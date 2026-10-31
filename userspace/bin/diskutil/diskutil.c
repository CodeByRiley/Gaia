/* userspace/bin/diskutil/diskutil.c , graphical view of disks and volumes.
 *
 * Two panes over the same selection. The top one is a table of every
 * volume , each partition, each disk that carries a filesystem directly,
 * and any filesystem mounted from a memory image , with its format, state,
 * size and free space. The bottom one draws each physical disk as a bar
 * with its partitions laid out by sector, so where things sit, and the
 * unallocated space between them, can be seen at a glance.
 *
 * Everything shown comes from three places:
 *
 *   SYS_BLKDEV_LIST   what volumes exist, and where each one starts
 *   SYS_MOUNT_LIST    what is mounted where, with the backend's free space
 *   SYS_BLKDEV_READ   the boot sector / superblock of unmounted volumes,
 *                     to name their format and label
 *
 * Mount and unmount are done in-process. Formatting and wiping are handed to
 * mkfs(8) and wipe(8) as child processes, so the window keeps redrawing
 * while they run and there is only one implementation of each. The kernel
 * refuses raw writes to a mounted volume, but not to the disk under a
 * mounted partition; the app adds that guard itself.
 *
 * Keys: F5 refresh, UP/DOWN select, M mount, U unmount, ESC quit.
 */
#include <include/key_codes.h>
#include <lib/app.h>
#include <lib/app_info.h>
#include <lib/gfx.h>
#include <lib/syscall.h>
#include <lib/ui.h>
#include <lib/wm.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/wait.h>

APP_INFO(APP_TYPE_GUI, "diskutil");

#define WIN_W 880
#define WIN_H 560

#define MENU_H 20
#define TOOL_H 26
#define ROW_H 14
#define SPLIT_H 6
#define LEGEND_H 18
#define DISK_H 62
#define DISK_GAP 6
#define INFO_W 108
#define SEG_MIN_W 112
#define STRIP_H 8

#define MAX_DEVS 64
#define MAX_MOUNTS 8
#define MAX_VOLS (MAX_DEVS + MAX_MOUNTS)
#define MAX_SEGS 32

/* Gaps up to this size are alignment padding (the MBR's first track, the
 * slack after a 1 MiB-aligned partition), not space anyone could use. */
#define UNALLOC_MIN_SECTORS 2048u
/* mkfs's own floor for FAT32: 65525 clusters plus the reserved area. */
#define FAT32_MIN_SECTORS 65536u

#define REFRESH_TICKS 1000

#define COL_WHITE 0x00FFFFFFu
#define COL_BLACK 0x00000000u
#define COL_PRIMARY 0x00000080u /* partition strip                        */
#define COL_WHOLE 0x00008080u   /* filesystem directly on a disk          */
#define COL_ESP 0x00800080u     /* EFI system partition                   */
#define COL_IMAGE 0x00808000u   /* memory-image mount                     */
#define COL_HATCH 0x00A0A0A0u
#define COL_SEL_FRAME 0x00000000u
#define COL_BUSY 0x00C06000u

#ifndef BLOCKDEV_OFFLINE
#define BLOCKDEV_OFFLINE 0u /* not in this ABI yet: nothing is ever offline */
#endif

enum {
  CMD_NONE = 0,
  CMD_REFRESH,
  CMD_MOUNT,
  CMD_MOUNT_AT,
  CMD_UNMOUNT,
  CMD_FORMAT,
  CMD_WIPE,
  CMD_SYNC,
  CMD_ABOUT,
  CMD_EXIT,
};

enum { MENU_ACTION = 1, MENU_HELP = 2 };

/* Toolbar ids for the *_id widgets. Kept clear of the auto-assigned ids the
 * simple widgets hand out from 1 upward. */
enum {
  ID_TOOL_BASE = 0x2000,
};

enum vol_kind { VOL_DEVICE, VOL_IMAGE };

struct volume {
  int kind;
  struct blockdev_info dev; /* VOL_DEVICE only                              */
  int parent;               /* volume index of the disk under a partition   */
  char fs[12];              /* "FAT32", "ext2", "RAW", ...                  */
  char label[17];
  int mounted;
  char target[MOUNT_PATH_MAX];
  uint64_t total_bytes, free_bytes; /* from the mount; 0 when unknown      */
};

/* Probing reads the disk, so its answer is kept by name between the
 * once-a-second refreshes and only redone when something may have changed
 * it (F5, format, wipe, unmount). */
struct probe {
  char name[BLOCKDEV_NAME_MAX];
  char fs[12];
  char label[17];
};

struct segment {
  int volume; /* -1 for unallocated                                       */
  uint64_t first, sectors;
};

struct job {
  long pid;
  int cmd; /* CMD_FORMAT or CMD_WIPE                                       */
  char device[BLOCKDEV_NAME_MAX];
};

struct diskutil {
  struct app app;
  struct ui_context ui;
  struct ui_menu_state menu, context;
  int mouse_x, mouse_y, buttons;

  struct volume vols[MAX_VOLS];
  int vol_count;
  int rows[MAX_VOLS]; /* table order: volume indices */
  int row_count;
  int disks[MAX_DEVS]; /* graph order: volume indices of whole disks */
  int disk_count;

  struct probe probes[MAX_DEVS];
  int probe_count;

  int selected; /* volume index, -1 for none */
  int table_scroll;
  int graph_scroll; /* first disk shown */
  int table_h;      /* splitter position, as the table's height */
  int dragging_split;

  int pending_cmd;
  struct job job;
  long last_refresh;

  char message[48]; /* transient status, overrides the selection summary */
  char status_shown[48];
};

static struct diskutil du;

/* ---------------- formatting ------------------------------------------- */

/* "16.96 GB" in the same binary units the screenshot of every disk tool
 * uses. Done in integers: the whole part and two decimals separately, so
 * a multi-terabyte disk cannot overflow on the way. */
static void fmt_size(char *out, size_t cap, uint64_t bytes) {
  static const char *units[] = {"bytes", "KB", "MB", "GB", "TB"};
  uint64_t unit              = 1;
  int u                      = 0;
  while (u < 4 && bytes >= unit * 1024u) {
    unit *= 1024u;
    u++;
  }
  if (u == 0) {
    snprintf(out, cap, "%llu bytes", (unsigned long long)bytes);
    return;
  }
  uint64_t whole = bytes / unit;
  uint64_t frac  = ((bytes % unit) * 100u) / unit;
  snprintf(out, cap, "%llu.%02llu %s", (unsigned long long)whole, (unsigned long long)frac, units[u]);
}

static uint64_t dev_bytes(const struct blockdev_info *d) { return d->sectors * d->sector_size; }

static uint64_t vol_capacity(const struct volume *v) {
  if (v->kind == VOL_DEVICE)
    return dev_bytes(&v->dev);
  return v->total_bytes;
}

static const char *vol_name(const struct volume *v) {
  return v->kind == VOL_DEVICE ? v->dev.name : "ramdisk";
}

static void copy_str(char *out, size_t cap, const char *in) {
  size_t n = 0;
  while (in && in[n] && n + 1 < cap) {
    out[n] = in[n];
    n++;
  }
  out[n] = 0;
}

static void set_message(const char *text) { copy_str(du.message, sizeof(du.message), text); }

static const char *errno_text(int e) {
  switch (e) {
  case ENOENT:
    return "no such volume";
  case ENODEV:
    return "unsupported or offline";
  case EBUSY:
    return "volume in use";
  case EINVAL:
    return "no filesystem found";
  case EIO:
    return "I/O error";
  default:
    return strerror(e);
  }
}

/* ---------------- probing ---------------------------------------------- */

static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const unsigned char *p) { return le16(p) | ((uint32_t)le16(p + 2) << 16); }

/* Copy an on-disk, space- or NUL-padded label and trim it. "NO NAME" is
 * FAT's spelling of no label at all. */
static void take_label(char *out, size_t cap, const unsigned char *src, size_t n) {
  size_t len = n < cap - 1 ? n : cap - 1;
  memcpy(out, src, len);
  out[len] = 0;
  for (size_t i = 0; i < len; i++)
    if ((unsigned char)out[i] < 0x20 || (unsigned char)out[i] > 0x7E)
      out[i] = 0;
  while (len && (out[len - 1] == ' ' || out[len - 1] == 0))
    out[--len] = 0;
  if (!strcmp(out, "NO NAME"))
    out[0] = 0;
}

/* Name what is on a volume from its first sectors. The same tests the
 * kernel's probes use, minus the parts that need a whole mount. */
static void probe_volume(const struct blockdev_info *d, struct probe *p) {
  static unsigned char boot[512], super[512];
  copy_str(p->name, sizeof(p->name), d->name);
  p->label[0] = 0;

  if (d->flags & BLOCKDEV_PARTITIONED) {
    copy_str(p->fs, sizeof(p->fs), "MBR");
    return;
  }
  if (blockdev_read(d->name, 0, 1, boot) != 1) {
    copy_str(p->fs, sizeof(p->fs), "Unreadable");
    return;
  }

  if (!memcmp(boot + 82, "FAT32", 5)) {
    copy_str(p->fs, sizeof(p->fs), "FAT32");
    take_label(p->label, sizeof(p->label), boot + 71, 11);
    return;
  }
  if (!memcmp(boot + 54, "FAT16", 5) || !memcmp(boot + 54, "FAT12", 5)) {
    memcpy(p->fs, boot + 54, 5);
    p->fs[5] = 0;
    take_label(p->label, sizeof(p->label), boot + 43, 11);
    return;
  }

  /* ext2's superblock is at byte 1024, LBA 2 of a 512-byte-sector disk. */
  if (blockdev_read(d->name, 2, 1, super) == 1 && le16(super + 56) == 0xEF53) {
    /* has_journal makes it ext3 to anyone else; Gaia mounts it as ext2. */
    copy_str(p->fs, sizeof(p->fs), le32(super + 92) & 0x4u ? "ext3" : "ext2");
    take_label(p->label, sizeof(p->label), super + 120, 16);
    return;
  }

  int blank = 1;
  for (int i = 0; i < 512 && blank; i++)
    blank = boot[i] == 0;
  copy_str(p->fs, sizeof(p->fs), blank ? "RAW" : "Unknown");
}

static const struct probe *probe_for(const struct blockdev_info *d) {
  for (int i = 0; i < du.probe_count; i++)
    if (!strcmp(du.probes[i].name, d->name))
      return &du.probes[i];
  if (du.probe_count >= MAX_DEVS)
    return 0;
  struct probe *p = &du.probes[du.probe_count++];
  probe_volume(d, p);
  return p;
}

static void forget_probe(const char *name) {
  for (int i = 0; i < du.probe_count; i++) {
    if (!strcmp(du.probes[i].name, name)) {
      du.probes[i] = du.probes[--du.probe_count];
      return;
    }
  }
}

/* ---------------- model ------------------------------------------------ */

/* "ahci0p3" -> index of "ahci0". Partition names are the parent's plus 'p'
 * and a number (kernel/drivers/storage/blockdev.h). */
static int find_parent(const char *name) {
  size_t len = strlen(name);
  size_t at  = len;
  while (at > 0 && name[at - 1] >= '0' && name[at - 1] <= '9')
    at--;
  if (at == len || at < 2 || name[at - 1] != 'p')
    return -1;
  for (int i = 0; i < du.vol_count; i++) {
    const struct volume *v = &du.vols[i];
    if (v->kind == VOL_DEVICE && strlen(v->dev.name) == at - 1 &&
        !strncmp(v->dev.name, name, at - 1))
      return i;
  }
  return -1;
}

static int same_volume(const struct volume *a, const struct volume *b) {
  if (a->kind != b->kind)
    return 0;
  if (a->kind == VOL_DEVICE)
    return !strcmp(a->dev.name, b->dev.name);
  return !strcmp(a->target, b->target);
}

/* Rebuild the volume list. The selection follows its volume by identity,
 * because indices shift when a disk appears or a mount comes and goes. */
static void refresh(int reprobe) {
  static struct blockdev_info devs[MAX_DEVS];
  static struct mount_info mounts[MAX_MOUNTS];
  static struct volume previous;
  int had_selection = du.selected >= 0 && du.selected < du.vol_count;
  if (had_selection)
    previous = du.vols[du.selected];

  if (reprobe)
    du.probe_count = 0;

  long dev_count = blockdev_list(devs, MAX_DEVS);
  if (dev_count < 0)
    dev_count = 0;
  long mount_count = mount_list(mounts, MAX_MOUNTS);
  if (mount_count < 0)
    mount_count = 0;

  du.vol_count = 0;
  for (long i = 0; i < dev_count; i++) {
    struct volume *v = &du.vols[du.vol_count++];
    memset(v, 0, sizeof(*v));
    v->kind = VOL_DEVICE;
    v->dev  = devs[i];
    /* Crossed the syscall boundary; bound it before anything prints it. */
    v->dev.name[BLOCKDEV_NAME_MAX - 1] = 0;
    v->parent                          = -1;
  }
  for (int i = 0; i < du.vol_count; i++)
    if (du.vols[i].dev.flags & BLOCKDEV_PARTITION)
      du.vols[i].parent = find_parent(du.vols[i].dev.name);

  for (long m = 0; m < mount_count; m++) {
    struct mount_info *mi            = &mounts[m];
    mi->source[BLOCKDEV_NAME_MAX - 1] = 0;
    mi->fstype[MOUNT_TYPE_MAX - 1]    = 0;
    mi->target[MOUNT_PATH_MAX - 1]    = 0;

    struct volume *v = 0;
    for (int i = 0; mi->source[0] && i < du.vol_count; i++)
      if (du.vols[i].kind == VOL_DEVICE && !strcmp(du.vols[i].dev.name, mi->source))
        v = &du.vols[i];
    if (!v) {
      if (du.vol_count >= MAX_VOLS)
        continue;
      v = &du.vols[du.vol_count++];
      memset(v, 0, sizeof(*v));
      v->kind   = VOL_IMAGE;
      v->parent = -1;
      copy_str(v->fs, sizeof(v->fs), mi->fstype);
    }
    v->mounted     = 1;
    v->total_bytes = mi->total_bytes;
    v->free_bytes  = mi->free_bytes;
    copy_str(v->target, sizeof(v->target), mi->target);
  }

  for (int i = 0; i < du.vol_count; i++) {
    struct volume *v = &du.vols[i];
    if (v->kind != VOL_DEVICE)
      continue;
    const struct probe *p = probe_for(&v->dev);
    if (p) {
      copy_str(v->fs, sizeof(v->fs), p->fs);
      copy_str(v->label, sizeof(v->label), p->label);
    }
  }

  /* Table: everything that can hold a filesystem, so not the disks whose
   * sector 0 is a partition table. Graph: every disk that is not itself a
   * slice of another, plus orphaned slices so nothing goes missing. */
  du.row_count  = 0;
  du.disk_count = 0;
  for (int i = 0; i < du.vol_count; i++) {
    const struct volume *v = &du.vols[i];
    if (!(v->kind == VOL_DEVICE && (v->dev.flags & BLOCKDEV_PARTITIONED)))
      du.rows[du.row_count++] = i;
    if (v->kind == VOL_DEVICE && v->parent < 0)
      du.disks[du.disk_count++] = i;
  }

  du.selected = -1;
  for (int i = 0; had_selection && i < du.vol_count; i++)
    if (same_volume(&du.vols[i], &previous))
      du.selected = i;
  if (du.selected < 0 && du.row_count > 0)
    du.selected = du.rows[0];

  du.last_refresh = get_ticks();
}

static struct volume *selection(void) {
  return du.selected >= 0 && du.selected < du.vol_count ? &du.vols[du.selected] : 0;
}

static int disk_of(int volume) {
  if (volume < 0)
    return -1;
  return du.vols[volume].parent >= 0 ? du.vols[volume].parent : volume;
}

/* True when any slice of disk `d` is mounted. Writing the disk under it
 * would pull a filesystem out from beneath its own cache. */
static int disk_has_mounted_child(int d) {
  for (int i = 0; i < du.vol_count; i++)
    if (du.vols[i].parent == d && du.vols[i].mounted)
      return 1;
  return 0;
}

static int job_running(void) { return du.job.pid > 0; }

static int job_targets(const struct volume *v) {
  return job_running() && v->kind == VOL_DEVICE && !strcmp(du.job.device, v->dev.name);
}

static int can_mount(const struct volume *v) {
  return v && v->kind == VOL_DEVICE && !v->mounted && !(v->dev.flags & BLOCKDEV_PARTITIONED) &&
         !(v->dev.flags & BLOCKDEV_OFFLINE) && !job_targets(v);
}

static int can_unmount(const struct volume *v) {
  return v && v->mounted && strcmp(v->target, "/") != 0;
}

static int can_write(const struct volume *v) {
  if (!v || v->kind != VOL_DEVICE || v->mounted || job_running())
    return 0;
  if (!(v->dev.flags & BLOCKDEV_WRITABLE) || (v->dev.flags & BLOCKDEV_OFFLINE))
    return 0;
  return !disk_has_mounted_child((int)(v - du.vols));
}

static int can_format(const struct volume *v) {
  return can_write(v) && v->dev.sectors >= FAT32_MIN_SECTORS && v->dev.sectors <= UINT32_MAX;
}

static const char *vol_status(const struct volume *v) {
  if (job_targets(v))
    return du.job.cmd == CMD_FORMAT ? "Formatting..." : "Wiping...";
  if (v->kind == VOL_DEVICE && (v->dev.flags & BLOCKDEV_OFFLINE))
    return "Offline";
  if (v->mounted && !strcmp(v->target, "/"))
    return "Healthy (System)";
  if (v->mounted)
    return "Healthy (Mounted)";
  if (v->dev.flags & BLOCKDEV_ESP)
    return "Healthy (EFI System)";
  if (!strcmp(v->fs, "RAW"))
    return "Unformatted";
  if (!strcmp(v->fs, "Unknown") || !strcmp(v->fs, "Unreadable"))
    return "Unrecognised";
  return "Healthy";
}

static const char *vol_layout(const struct volume *v) {
  if (v->kind == VOL_IMAGE)
    return "Memory";
  return v->dev.flags & BLOCKDEV_PARTITION ? "Partition" : "Whole disk";
}

static const char *vol_type(const struct volume *v) {
  if (v->kind == VOL_IMAGE)
    return "Image";
  return v->dev.flags & BLOCKDEV_REMOVABLE ? "Removable" : "Basic";
}

static uint32_t vol_color(const struct volume *v) {
  if (v->kind == VOL_IMAGE)
    return COL_IMAGE;
  if (v->dev.flags & BLOCKDEV_ESP)
    return COL_ESP;
  return v->dev.flags & BLOCKDEV_PARTITION ? COL_PRIMARY : COL_WHOLE;
}

/* "SYSTEM (ahci0p2)" when there is a label, as other disk tools pair a
 * label with its drive letter. */
static void vol_title(const struct volume *v, char *out, size_t cap) {
  if (v->label[0])
    snprintf(out, cap, "%s (%s)", v->label, vol_name(v));
  else
    snprintf(out, cap, "%s", vol_name(v));
}

/* ---------------- layout ----------------------------------------------- */

struct layout {
  struct gfx_rect menu, tools, table, split, graph, legend;
};

static struct layout compute_layout(const struct gfx_surface *s) {
  struct layout l;
  l.menu  = gfx_rect_make(0, 0, s->w, MENU_H);
  l.tools = gfx_rect_make(0, MENU_H, s->w, TOOL_H);

  int top    = MENU_H + TOOL_H + 2;
  int bottom = s->h - LEGEND_H;
  int room   = bottom - top - SPLIT_H;
  if (du.table_h <= 0)
    du.table_h = room * 2 / 5;
  if (du.table_h > room - DISK_H)
    du.table_h = room - DISK_H;
  if (du.table_h < ROW_H * 3)
    du.table_h = ROW_H * 3;

  l.table  = gfx_rect_make(2, top, s->w - 4, du.table_h);
  l.split  = gfx_rect_make(0, top + du.table_h, s->w, SPLIT_H);
  l.graph  = gfx_rect_make(2, top + du.table_h + SPLIT_H, s->w - 4, bottom - (top + du.table_h + SPLIT_H));
  l.legend = gfx_rect_make(0, bottom, s->w, LEGEND_H);
  return l;
}

static int table_visible_rows(struct gfx_rect table) {
  int rows = (gfx_rect_inset(table, 2).h - ROW_H) / ROW_H;
  return rows < 1 ? 1 : rows;
}

static int graph_visible_disks(struct gfx_rect graph) {
  int n = (gfx_rect_inset(graph, 2).h - DISK_GAP) / (DISK_H + DISK_GAP);
  return n < 1 ? 1 : n;
}

/* Scroll both panes so the selection is on screen in each. */
static void reveal_selection(const struct layout *l) {
  int rows = table_visible_rows(l->table);
  for (int r = 0; r < du.row_count; r++) {
    if (du.rows[r] != du.selected)
      continue;
    if (r < du.table_scroll)
      du.table_scroll = r;
    if (r >= du.table_scroll + rows)
      du.table_scroll = r - rows + 1;
  }
  if (du.table_scroll > du.row_count - rows)
    du.table_scroll = du.row_count - rows;
  if (du.table_scroll < 0)
    du.table_scroll = 0;

  int disks = graph_visible_disks(l->graph);
  int d     = disk_of(du.selected);
  for (int i = 0; i < du.disk_count; i++) {
    if (du.disks[i] != d)
      continue;
    if (i < du.graph_scroll)
      du.graph_scroll = i;
    if (i >= du.graph_scroll + disks)
      du.graph_scroll = i - disks + 1;
  }
  if (du.graph_scroll > du.disk_count - disks)
    du.graph_scroll = du.disk_count - disks;
  if (du.graph_scroll < 0)
    du.graph_scroll = 0;
}

/* ---------------- drawing: shared -------------------------------------- */

static int pressed_in(struct gfx_rect r) {
  struct ui_context *ui = &du.ui;
  int left  = ui->down && !ui->was_down;
  int right = (ui->buttons & MOUSE_BTN_RIGHT) && !(ui->was_buttons & MOUSE_BTN_RIGHT);
  /* A press while a popup is open belongs to the popup. */
  if (du.menu.open || du.context.open)
    return 0;
  return (left || right) && gfx_rect_contains(r, ui->mx, ui->my);
}

static void select_volume(int volume) {
  if (volume == du.selected)
    return;
  du.selected   = volume;
  du.message[0] = 0;
  app_request_redraw(&du.app);
}

/* Text clipped to a cell, vertically centred in it. */
static void cell_text(struct gfx_surface *s, struct gfx_rect r, const char *text, uint32_t color) {
  struct gfx_rect prev = gfx_clip_push(s, r);
  gfx_text(s, r.x + 3, r.y + (r.h - 8) / 2, text, color, 1);
  gfx_clip_set(s, prev);
}

static void hatch(struct gfx_surface *s, struct gfx_rect r, uint32_t color) {
  for (int y = 0; y < r.h; y++)
    for (int x = (6 - ((r.x + r.y + y) % 6)) % 6; x < r.w; x += 6)
      gfx_pixel(s, r.x + x, r.y + y, color);
}

/* A beveled toolbar button that can be shown disabled. lib/ui has no
 * disabled state, and skipping the call would renumber nothing here (these
 * use fixed ids) but would make the toolbar jump around. */
static int tool_button(int id, struct gfx_rect r, const char *label, int enabled) {
  if (enabled)
    return ui_button_id(&du.ui, id, r, label);
  const struct ui_theme *t = du.ui.theme;
  gfx_fill(du.ui.s, r, t->face);
  gfx_bevel(du.ui.s, r, t->light, t->dark, 1);
  int tw = 0, th = 0;
  gfx_text_size(label, 1, &tw, &th);
  gfx_text(du.ui.s, r.x + (r.w - tw) / 2 + 1, r.y + (r.h - th) / 2 + 1, label, t->light, 1);
  gfx_text(du.ui.s, r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2, label, t->text_muted, 1);
  return 0;
}

/* ---------------- drawing: table --------------------------------------- */

static const char *const columns[] = {
    "Volume",   "Mount Point", "Layout",     "Type",   "File System",
    "Status",   "Capacity",    "Free Space", "% Free",
};
#define COLUMN_COUNT ((int)(sizeof(columns) / sizeof(columns[0])))
#define CELL_PAD 10
#define SWATCH_W 12 /* colour key ahead of the volume name */

static int text_w(const char *text) {
  int w = 0, h = 0;
  gfx_text_size(text, 1, &w, &h);
  return w;
}

static void row_cells(const struct volume *v, char cells[COLUMN_COUNT][48]) {
  vol_title(v, cells[0], sizeof(cells[0]));
  copy_str(cells[1], sizeof(cells[1]), v->mounted ? v->target : "");
  copy_str(cells[2], sizeof(cells[2]), vol_layout(v));
  copy_str(cells[3], sizeof(cells[3]), vol_type(v));
  copy_str(cells[4], sizeof(cells[4]), v->fs[0] ? v->fs : "-");
  copy_str(cells[5], sizeof(cells[5]), vol_status(v));
  fmt_size(cells[6], sizeof(cells[6]), vol_capacity(v));
  if (v->mounted && v->total_bytes) {
    fmt_size(cells[7], sizeof(cells[7]), v->free_bytes);
    snprintf(cells[8],
             sizeof(cells[8]),
             "%llu %%",
             (unsigned long long)(v->free_bytes * 100u / v->total_bytes));
  } else {
    copy_str(cells[7], sizeof(cells[7]), "-");
    copy_str(cells[8], sizeof(cells[8]), "-");
  }
}

/* Size every column to its widest cell, so nothing is cut off while there
 * is room; whatever is left over goes to the last column. Measured, not
 * counted in characters, because the system font is not 8 pixels wide. */
static void column_widths(int avail, int *out) {
  for (int c = 0; c < COLUMN_COUNT; c++)
    out[c] = text_w(columns[c]) + CELL_PAD;
  out[0] += SWATCH_W;
  for (int r = 0; r < du.row_count; r++) {
    char cells[COLUMN_COUNT][48];
    row_cells(&du.vols[du.rows[r]], cells);
    for (int c = 0; c < COLUMN_COUNT; c++) {
      int w = text_w(cells[c]) + CELL_PAD + (c == 0 ? SWATCH_W : 0);
      if (w > out[c])
        out[c] = w;
    }
  }
  int used = 0;
  for (int c = 0; c < COLUMN_COUNT; c++)
    used += out[c];
  if (used < avail)
    out[COLUMN_COUNT - 1] += avail - used;
}

static void draw_table(struct gfx_surface *s, struct gfx_rect r) {
  const struct ui_theme *t = du.ui.theme;
  ui_well(&du.ui, r);
  struct gfx_rect in = gfx_rect_inset(r, 2);
  gfx_fill(s, in, COL_WHITE);
  struct gfx_rect prev = gfx_clip_push(s, in);

  int widths[COLUMN_COUNT];
  column_widths(in.w, widths);

  /* Header: one raised cell per column, like a list view's. */
  int x = in.x;
  for (int c = 0; c < COLUMN_COUNT; c++) {
    struct gfx_rect cell = gfx_rect_make(x, in.y, widths[c], ROW_H);
    gfx_fill(s, cell, t->face);
    gfx_bevel(s, cell, t->light, t->dark, 1);
    cell_text(s, cell, columns[c], t->text);
    x += widths[c];
  }

  int rows = table_visible_rows(r);
  for (int i = 0; i < rows; i++) {
    int index = du.table_scroll + i;
    if (index >= du.row_count)
      break;
    int vi                 = du.rows[index];
    const struct volume *v = &du.vols[vi];
    struct gfx_rect line   = gfx_rect_make(in.x, in.y + ROW_H * (i + 1), in.w, ROW_H);

    if (pressed_in(line))
      select_volume(vi);

    int sel        = vi == du.selected;
    uint32_t fg    = sel ? t->accent_text : t->text;
    if (sel)
      gfx_fill(s, line, t->accent);

    char cells[COLUMN_COUNT][48];
    row_cells(v, cells);

    x = in.x;
    for (int c = 0; c < COLUMN_COUNT; c++) {
      struct gfx_rect cell = gfx_rect_make(x, line.y, widths[c], ROW_H);
      if (c == 0) {
        /* Swatch in the volume's graph colour ties the two panes together. */
        gfx_fill(s, gfx_rect_make(cell.x + 3, cell.y + 3, 8, 8), vol_color(v));
        gfx_frame(s, gfx_rect_make(cell.x + 3, cell.y + 3, 8, 8), sel ? COL_WHITE : t->dark, 1);
        cell = gfx_rect_make(cell.x + SWATCH_W, cell.y, cell.w - SWATCH_W, cell.h);
      }
      uint32_t color = fg;
      if (c == 5 && !sel && job_targets(v))
        color = COL_BUSY;
      cell_text(s, cell, cells[c], color);
      x += widths[c];
    }
  }

  if (du.row_count == 0)
    gfx_text(s, in.x + 6, in.y + ROW_H + 4, "No volumes found", t->text_muted, 1);

  gfx_clip_set(s, prev);
}

/* ---------------- drawing: disk graph ---------------------------------- */

/* Lay disk `d` out as an ordered run of partitions and the unallocated gaps
 * between them. A disk with no table is one segment: itself. */
static int disk_segments(int d, struct segment *out) {
  const struct volume *disk = &du.vols[d];
  int n                     = 0;

  if (!(disk->dev.flags & BLOCKDEV_PARTITIONED)) {
    out[n++] = (struct segment){d, 0, disk->dev.sectors};
    return n;
  }

  int order[MAX_SEGS];
  int count = 0;
  for (int i = 0; i < du.vol_count && count < MAX_SEGS; i++)
    if (du.vols[i].parent == d)
      order[count++] = i;
  /* Insertion sort by start: the table is a handful of entries at most. */
  for (int i = 1; i < count; i++) {
    int key = order[i], j = i - 1;
    while (j >= 0 && du.vols[order[j]].dev.start_lba > du.vols[key].dev.start_lba) {
      order[j + 1] = order[j];
      j--;
    }
    order[j + 1] = key;
  }

  uint64_t at = 0;
  for (int i = 0; i < count && n < MAX_SEGS - 1; i++) {
    const struct volume *p = &du.vols[order[i]];
    if (p->dev.start_lba > at && p->dev.start_lba - at > UNALLOC_MIN_SECTORS)
      out[n++] = (struct segment){-1, at, p->dev.start_lba - at};
    out[n++] = (struct segment){order[i], p->dev.start_lba, p->dev.sectors};
    uint64_t end = p->dev.start_lba + p->dev.sectors;
    if (end > at)
      at = end;
  }
  if (disk->dev.sectors > at && disk->dev.sectors - at > UNALLOC_MIN_SECTORS && n < MAX_SEGS)
    out[n++] = (struct segment){-1, at, disk->dev.sectors - at};
  if (n == 0)
    out[n++] = (struct segment){-1, 0, disk->dev.sectors};
  return n;
}

/* Widths proportional to size, but never so thin a small slice (an ESP
 * next to a big data partition) has no room for its name. */
static void segment_widths(const struct segment *segs, int n, uint64_t total, int width, int *out) {
  if (n * SEG_MIN_W >= width || total == 0) {
    for (int i = 0; i < n; i++)
      out[i] = width / n;
    out[n - 1] += width - (width / n) * n;
    return;
  }
  int spare = width - n * SEG_MIN_W;
  int used  = 0;
  for (int i = 0; i < n; i++) {
    out[i] = SEG_MIN_W + (int)((segs[i].sectors * (uint64_t)spare) / total);
    used += out[i];
  }
  out[n - 1] += width - used; /* rounding remainder */
}

static void draw_segment(struct gfx_surface *s, struct gfx_rect r, const struct segment *seg,
                         int disk) {
  const struct ui_theme *t = du.ui.theme;
  int vi                   = seg->volume;
  int sel                  = vi >= 0 && vi == du.selected;

  if (pressed_in(r))
    select_volume(vi >= 0 ? vi : disk);

  gfx_fill(s, r, COL_WHITE);
  gfx_fill(s, gfx_rect_make(r.x, r.y, r.w, STRIP_H), vi >= 0 ? vol_color(&du.vols[vi]) : COL_BLACK);
  struct gfx_rect body = gfx_rect_make(r.x, r.y + STRIP_H, r.w, r.h - STRIP_H);
  if (vi < 0)
    hatch(s, body, COL_HATCH);
  gfx_frame(s, r, t->dark, 1);

  char line1[48], line2[48], size[24];
  const char *line3;
  fmt_size(size, sizeof(size), seg->sectors * 512u);
  if (vi >= 0) {
    const struct volume *v = &du.vols[vi];
    vol_title(v, line1, sizeof(line1));
    snprintf(line2, sizeof(line2), "%s %s", size, v->fs);
    line3 = vol_status(v);
  } else {
    copy_str(line1, sizeof(line1), size);
    line2[0] = 0;
    line3    = "Unallocated";
  }

  struct gfx_rect text = gfx_rect_inset(body, 4);
  struct gfx_rect prev = gfx_clip_push(s, text);
  gfx_text(s, text.x, text.y + 2, line1, t->text, 1);
  gfx_text(s, text.x, text.y + 14, line2, t->text, 1);
  gfx_text(s, text.x, text.y + 26, line3, vi >= 0 && job_targets(&du.vols[vi]) ? COL_BUSY : t->text, 1);
  gfx_clip_set(s, prev);

  /* Selection as a heavy inner frame, so the colours underneath still read. */
  if (sel)
    gfx_frame(s, gfx_rect_inset(r, 2), COL_SEL_FRAME, 2);
}

static void draw_disk(struct gfx_surface *s, struct gfx_rect r, int number, int d) {
  const struct ui_theme *t  = du.ui.theme;
  const struct volume *disk = &du.vols[d];

  /* Info box on the left: raised, like the disk headers in the screenshot of
   * any volume manager. Clicking it selects the whole disk. */
  struct gfx_rect info = gfx_rect_make(r.x, r.y, INFO_W, r.h);
  if (pressed_in(info))
    select_volume(d);
  gfx_fill(s, info, t->face);
  gfx_bevel(s, info, t->light, t->dark, 1);
  if (d == du.selected)
    gfx_frame(s, gfx_rect_inset(info, 2), COL_SEL_FRAME, 2);

  char line[32], size[24];
  struct gfx_rect prev = gfx_clip_push(s, gfx_rect_inset(info, 3));
  snprintf(line, sizeof(line), "Disk %d", number);
  gfx_text(s, info.x + 6, info.y + 5, line, t->text, 1);
  gfx_text(s, info.x + 6, info.y + 16, disk->dev.name, t->text_muted, 1);
  gfx_text(s, info.x + 6, info.y + 27, vol_type(disk), t->text, 1);
  fmt_size(size, sizeof(size), dev_bytes(&disk->dev));
  gfx_text(s, info.x + 6, info.y + 38, size, t->text, 1);
  const char *state = (disk->dev.flags & BLOCKDEV_OFFLINE)    ? "Offline"
                      : (disk->dev.flags & BLOCKDEV_WRITABLE) ? "Online"
                                                              : "Read-only";
  gfx_text(s, info.x + 6, info.y + 49, state, t->text, 1);
  gfx_clip_set(s, prev);

  struct segment segs[MAX_SEGS];
  int widths[MAX_SEGS];
  int n = disk_segments(d, segs);
  struct gfx_rect bar = gfx_rect_make(r.x + INFO_W + 2, r.y, r.w - INFO_W - 2, r.h);
  segment_widths(segs, n, disk->dev.sectors, bar.w, widths);

  int x = bar.x;
  for (int i = 0; i < n; i++) {
    draw_segment(s, gfx_rect_make(x, bar.y, widths[i] - 2, bar.h), &segs[i], d);
    x += widths[i];
  }
}

static void draw_graph(struct gfx_surface *s, struct gfx_rect r) {
  const struct ui_theme *t = du.ui.theme;
  ui_well(&du.ui, r);
  struct gfx_rect in = gfx_rect_inset(r, 2);
  gfx_fill(s, in, t->face);
  struct gfx_rect prev = gfx_clip_push(s, in);

  int shown = graph_visible_disks(r);
  int y     = in.y + DISK_GAP;
  for (int i = 0; i < shown && du.graph_scroll + i < du.disk_count; i++) {
    int idx = du.graph_scroll + i;
    draw_disk(s, gfx_rect_make(in.x + DISK_GAP, y, in.w - DISK_GAP * 2, DISK_H), idx, du.disks[idx]);
    y += DISK_H + DISK_GAP;
  }
  if (du.disk_count == 0)
    gfx_text(s, in.x + 8, in.y + 8, "No disks found", t->text_muted, 1);
  else if (du.disk_count > shown) {
    char more[40];
    snprintf(more, sizeof(more), "disks %d-%d of %d", du.graph_scroll + 1,
             du.graph_scroll + shown < du.disk_count ? du.graph_scroll + shown : du.disk_count,
             du.disk_count);
    gfx_text(s, in.x + in.w - text_w(more) - 6, in.y + in.h - 11, more, t->text_muted, 1);
  }

  gfx_clip_set(s, prev);
}

static void draw_legend(struct gfx_surface *s, struct gfx_rect r) {
  const struct ui_theme *t = du.ui.theme;
  ui_panel(&du.ui, r);
  static const struct {
    uint32_t color;
    const char *label;
  } keys[] = {
      {COL_PRIMARY, "Partition"  },
      {COL_WHOLE,   "Whole disk" },
      {COL_ESP,     "EFI system" },
      {COL_IMAGE,   "Memory"     },
      {COL_BLACK,   "Unallocated"},
  };
  int x = r.x + 8;
  for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
    struct gfx_rect sw = gfx_rect_make(x, r.y + 5, 10, 8);
    gfx_fill(s, sw, keys[i].color);
    gfx_frame(s, sw, t->dark, 1);
    gfx_text(s, x + 14, r.y + 5, keys[i].label, t->text, 1);
    x += 14 + text_w(keys[i].label) + 16;
  }
}

/* ---------------- drawing: chrome -------------------------------------- */

static struct ui_menu_item action_items[] = {
    {"Refresh           F5", CMD_REFRESH,  1},
    {"Mount              M", CMD_MOUNT,    1},
    {"Mount At...",          CMD_MOUNT_AT, 1},
    {"Unmount            U", CMD_UNMOUNT,  1},
    {"Format FAT32...",      CMD_FORMAT,   1},
    {"Wipe...",              CMD_WIPE,     1},
    {"Sync All",             CMD_SYNC,     1},
    {"Exit",                 CMD_EXIT,     1},
};
static const struct ui_menu_item help_items[] = {
    {"About Disk Utility", CMD_ABOUT, 1},
};
/* Same commands, minus the ones that are not about the clicked volume. */
static struct ui_menu_item context_items[] = {
    {"Mount",           CMD_MOUNT,    1},
    {"Mount At...",     CMD_MOUNT_AT, 1},
    {"Unmount",         CMD_UNMOUNT,  1},
    {"Format FAT32...", CMD_FORMAT,   1},
    {"Wipe...",         CMD_WIPE,     1},
};
#define ITEM_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void update_enabled(const struct volume *v) {
  int mount = can_mount(v), unmount = can_unmount(v), format = can_format(v), wipe = can_write(v);
  action_items[1].enabled  = mount;
  action_items[2].enabled  = mount;
  action_items[3].enabled  = unmount;
  action_items[4].enabled  = format;
  action_items[5].enabled  = wipe;
  context_items[0].enabled = mount;
  context_items[1].enabled = mount;
  context_items[2].enabled = unmount;
  context_items[3].enabled = format;
  context_items[4].enabled = wipe;
}

static void draw_toolbar(struct gfx_rect r, const struct volume *v) {
  ui_panel(&du.ui, r);
  static const struct {
    const char *label;
    int cmd;
    int w;
  } tools[] = {
      {"Refresh",   CMD_REFRESH,  72},
      {"Mount",     CMD_MOUNT,    56},
      {"Unmount",   CMD_UNMOUNT,  72},
      {"Format...", CMD_FORMAT,   88},
      {"Wipe...",   CMD_WIPE,     72},
      {"Sync",      CMD_SYNC,     48},
  };
  int x = r.x + 4;
  for (int i = 0; i < ITEM_COUNT(tools); i++) {
    int enabled = 1;
    switch (tools[i].cmd) {
    case CMD_MOUNT:
      enabled = can_mount(v);
      break;
    case CMD_UNMOUNT:
      enabled = can_unmount(v);
      break;
    case CMD_FORMAT:
      enabled = can_format(v);
      break;
    case CMD_WIPE:
      enabled = can_write(v);
      break;
    default:
      break;
    }
    struct gfx_rect b = gfx_rect_make(x, r.y + 3, tools[i].w, r.h - 6);
    if (tool_button(ID_TOOL_BASE + i, b, tools[i].label, enabled))
      du.pending_cmd = tools[i].cmd;
    x += tools[i].w + 4;
    /* A gap between the view command and the ones that act on a volume. */
    if (i == 0)
      x += 8;
  }
}

/* The status strip shows a transient message when there is one, otherwise
 * what the selection is. Only sent when it changes: each set is an IPC. */
static void update_status(const struct volume *v) {
  char text[48];
  if (du.message[0])
    copy_str(text, sizeof(text), du.message);
  else if (v && v->kind == VOL_DEVICE)
    snprintf(text,
             sizeof(text),
             "%s  LBA %llu  %llu sectors  %s",
             v->dev.name,
             (unsigned long long)v->dev.start_lba,
             (unsigned long long)v->dev.sectors,
             v->dev.flags & BLOCKDEV_WRITABLE ? "rw" : "ro");
  else if (v)
    snprintf(text, sizeof(text), "memory image mounted at %s", v->target);
  else
    copy_str(text, sizeof(text), "F5 refresh  M mount  U unmount  ESC quit");

  if (strcmp(text, du.status_shown)) {
    copy_str(du.status_shown, sizeof(du.status_shown), text);
    wm_window_set_status(du.app.window.handle, text);
  }
}

static void diskutil_draw(struct app *a, void *ctx) {
  (void)ctx;
  struct gfx_surface *s  = &a->surface;
  struct layout l        = compute_layout(s);
  const struct ui_theme *t;

  ui_begin(&du.ui, s, 0, du.mouse_x, du.mouse_y, du.buttons);
  t = du.ui.theme;
  gfx_clear(s, t->face);

  /* Splitter drag runs before anything reads the layout. */
  if (pressed_in(l.split) && du.ui.down)
    du.dragging_split = 1;
  if (du.dragging_split) {
    if (du.ui.down) {
      du.table_h = du.ui.my - l.table.y - SPLIT_H / 2;
      l          = compute_layout(s);
    } else {
      du.dragging_split = 0;
    }
  }
  reveal_selection(&l);

  const struct volume *v = selection();
  update_enabled(v);

  ui_panel(&du.ui, l.menu);
  draw_toolbar(l.tools, v);
  draw_table(s, l.table);
  gfx_hline(s, 0, l.split.y + 2, s->w, t->dark);
  gfx_hline(s, 0, l.split.y + 3, s->w, t->light);
  draw_graph(s, l.graph);
  draw_legend(s, l.legend);

  /* Right-click: the press above already selected what is under the
   * pointer, so the menu acts on it. */
  struct gfx_rect views = gfx_rect_union(l.table, l.graph);
  int cmd = ui_context_menu(&du.ui, &du.context, views, context_items, ITEM_COUNT(context_items));

  /* Menus last, so their popups paint over both panes. */
  int m = ui_menu_bar_item(&du.ui, &du.menu, MENU_ACTION, gfx_rect_make(2, 1, 56, MENU_H - 2),
                           "Action", action_items, ITEM_COUNT(action_items));
  if (!m)
    m = ui_menu_bar_item(&du.ui, &du.menu, MENU_HELP, gfx_rect_make(60, 1, 40, MENU_H - 2), "Help",
                         help_items, ITEM_COUNT(help_items));
  if (m)
    cmd = m;
  if (cmd)
    du.pending_cmd = cmd;

  ui_end(&du.ui);
  update_status(selection());
}

/* ---------------- commands --------------------------------------------- */

static void default_target(const struct volume *v, char *out, size_t cap) {
  snprintf(out, cap, "/mnt/%s", v->dev.name);
}

static void do_mount(struct volume *v, const char *target) {
  char msg[96];
  if (mount(v->dev.name, target, NULL, 0, NULL) != 0) {
    int e = errno;
    snprintf(msg, sizeof(msg), "Mount %s failed: %s", v->dev.name, errno_text(e));
    set_message(msg);
    if (e == EINVAL)
      wm_prompt(du.app.window.handle, WM_PROMPT_MESSAGE, "No filesystem Gaia can mount.", 0, 0);
    return;
  }
  snprintf(msg, sizeof(msg), "Mounted %s at %s", v->dev.name, target);
  set_message(msg);
}

static void do_unmount(struct volume *v) {
  char msg[96];
  char name[24];
  copy_str(name, sizeof(name), vol_name(v));
  if (umount(v->target) != 0) {
    snprintf(msg, sizeof(msg), "Unmount %s failed: %s", name, errno_text(errno));
    set_message(msg);
    return;
  }
  /* The unmount's final sync may have written; read the volume afresh. */
  if (v->kind == VOL_DEVICE)
    forget_probe(v->dev.name);
  snprintf(msg, sizeof(msg), "Unmounted %s", name);
  set_message(msg);
}

/* Hand a destructive job to its tool. Confirmed here, because the tools
 * themselves ask nothing , wipe's --all is its only latch. */
static void start_job(struct volume *v, int cmd) {
  char question[48];
  int whole = (v->dev.flags & BLOCKDEV_PARTITIONED) != 0;
  if (whole)
    snprintf(question, sizeof(question), "Erase %s and ALL its partitions?", v->dev.name);
  else if (cmd == CMD_FORMAT)
    snprintf(question, sizeof(question), "Format %s as FAT32? Data is lost.", v->dev.name);
  else
    snprintf(question, sizeof(question), "Wipe every sector of %s?", v->dev.name);
  if (wm_prompt(du.app.window.handle, WM_PROMPT_CONFIRM, question, 0, 0) != WM_PROMPT_OK) {
    set_message("Cancelled");
    return;
  }

  static char device[BLOCKDEV_NAME_MAX];
  copy_str(device, sizeof(device), v->dev.name);
  char *format_argv[] = {"mkfs", device, "fat32", 0};
  char *wipe_argv[]   = {"wipe", device, "--all", 0};
  long pid = cmd == CMD_FORMAT ? spawn("/system/bin/mkfs.elf", format_argv)
                               : spawn("/system/bin/wipe.elf", wipe_argv);
  char msg[64];
  if (pid <= 0) {
    snprintf(msg, sizeof(msg), "Could not start %s", cmd == CMD_FORMAT ? "mkfs" : "wipe");
    set_message(msg);
    return;
  }
  du.job.pid = pid;
  du.job.cmd = cmd;
  copy_str(du.job.device, sizeof(du.job.device), device);
  snprintf(msg, sizeof(msg), "%s %s...", cmd == CMD_FORMAT ? "Formatting" : "Wiping", device);
  set_message(msg);
}

static int poll_job(void) {
  if (!job_running())
    return 0;
  int status = 0;
  pid_t r    = waitpid((pid_t)du.job.pid, &status, WNOHANG);
  if (r == 0)
    return 0;

  char msg[64];
  const char *what = du.job.cmd == CMD_FORMAT ? "Format" : "Wipe";
  if (r < 0)
    snprintf(msg, sizeof(msg), "%s of %s: lost track of it", what, du.job.device);
  else if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    snprintf(msg, sizeof(msg), "%s of %s complete", what, du.job.device);
  else
    snprintf(msg, sizeof(msg), "%s of %s failed (see console)", what, du.job.device);
  set_message(msg);
  forget_probe(du.job.device);
  du.job.pid = 0;
  refresh(0);
  return 1;
}

static int run_command(int cmd) {
  struct volume *v = selection();
  switch (cmd) {
  case CMD_REFRESH:
    refresh(1);
    set_message("Refreshed");
    break;
  case CMD_MOUNT:
    if (can_mount(v)) {
      char target[MOUNT_PATH_MAX];
      default_target(v, target, sizeof(target));
      do_mount(v, target);
      refresh(0);
    }
    break;
  case CMD_MOUNT_AT:
    if (can_mount(v)) {
      char target[MOUNT_PATH_MAX];
      char question[48];
      snprintf(question, sizeof(question), "Mount %s at (path):", v->dev.name);
      target[0] = 0;
      if (wm_prompt(du.app.window.handle, WM_PROMPT_TEXT, question, target, sizeof(target)) ==
              WM_PROMPT_OK &&
          target[0]) {
        do_mount(v, target);
        refresh(0);
      }
    }
    break;
  case CMD_UNMOUNT:
    if (can_unmount(v)) {
      do_unmount(v);
      refresh(0);
    }
    break;
  case CMD_FORMAT:
    if (can_format(v))
      start_job(v, CMD_FORMAT);
    break;
  case CMD_WIPE:
    if (can_write(v))
      start_job(v, CMD_WIPE);
    break;
  case CMD_SYNC:
    set_message(fs_sync() == 0 ? "All volumes synced" : "Sync failed on a volume");
    refresh(0);
    break;
  case CMD_ABOUT:
    wm_prompt(du.app.window.handle, WM_PROMPT_MESSAGE, "Disk Utility - volumes and mounts", 0, 0);
    break;
  case CMD_EXIT:
    return APP_ACTION_EXIT;
  default:
    return APP_ACTION_NONE;
  }
  return APP_ACTION_REDRAW;
}

/* ---------------- input ------------------------------------------------ */

static void select_row_delta(int delta) {
  if (du.row_count == 0)
    return;
  int at = 0;
  for (int r = 0; r < du.row_count; r++)
    if (du.rows[r] == du.selected)
      at = r;
  at += delta;
  if (at < 0)
    at = 0;
  if (at >= du.row_count)
    at = du.row_count - 1;
  select_volume(du.rows[at]);
}

static int diskutil_event(struct app *a, const struct wm_event *ev, void *ctx) {
  (void)a;
  (void)ctx;
  switch (ev->type) {
  case WM_EV_KEY_DOWN:
    switch (ev->param) {
    case KEY_ESC:
      if (du.menu.open || du.context.open) {
        du.menu.open    = 0;
        du.context.open = 0;
        break;
      }
      return APP_ACTION_EXIT;
    case KEY_UP:
      select_row_delta(-1);
      break;
    case KEY_DOWN:
      select_row_delta(1);
      break;
    case KEY_PAGEUP:
      select_row_delta(-8);
      break;
    case KEY_PAGEDOWN:
      select_row_delta(8);
      break;
    case KEY_F5:
      du.pending_cmd = CMD_REFRESH;
      break;
    case KEY_M:
      du.pending_cmd = CMD_MOUNT;
      break;
    case KEY_U:
      du.pending_cmd = CMD_UNMOUNT;
      break;
    default:
      return APP_ACTION_NONE;
    }
    return APP_ACTION_REDRAW;
  case WM_EV_MOUSE_MOVE:
  case WM_EV_MOUSE_DOWN:
  case WM_EV_MOUSE_UP:
    du.mouse_x = ev->x;
    du.mouse_y = ev->y;
    du.buttons = ev->param;
    return APP_ACTION_REDRAW;
  default:
    return APP_ACTION_NONE;
  }
}

/* Commands run here rather than in draw: several block in wm_prompt, and a
 * dialog should not open halfway through painting a frame. */
static int diskutil_idle(struct app *a, void *ctx) {
  (void)a;
  (void)ctx;
  if (du.pending_cmd) {
    int cmd        = du.pending_cmd;
    du.pending_cmd = CMD_NONE;
    return run_command(cmd);
  }
  int changed = poll_job();

  /* Pick up mounts made from a shell and disks that come and go. Probes
   * are cached, so this costs two syscalls unless something is new. */
  if (get_ticks() - du.last_refresh >= REFRESH_TICKS) {
    static struct volume before[MAX_VOLS];
    int before_count = du.vol_count;
    memcpy(before, du.vols, sizeof(before));
    refresh(0);
    if (du.vol_count != before_count || memcmp(before, du.vols, sizeof(du.vols[0]) * du.vol_count))
      changed = 1;
  }
  return changed ? APP_ACTION_REDRAW : APP_ACTION_NONE;
}

int main(void) {
  du.selected = -1;
  if (app_open_ex(&du.app, WIN_W, WIN_H, "Disk Utility", WM_CREATE_STATUSBAR) != 0) {
    printf("diskutil: app_open failed\n");
    return 1;
  }
  refresh(1);

  static const struct app_callbacks cb = {
      .event = diskutil_event,
      .draw  = diskutil_draw,
      .idle  = diskutil_idle,
  };
  return app_run(&du.app, &cb, &du);
}
