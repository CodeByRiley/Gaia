#include <include/key_codes.h>
#include <lib/app.h>
#include <lib/app_info.h>
#include <lib/damage.h>
#include <lib/gfx.h>
#include <lib/keymap.h>
#include <lib/syscall.h>
#include <lib/ttf.h>
#include <lib/ui.h>
#include <lib/wm.h>
#include <stdio.h>
#include <string.h>

APP_INFO(APP_TYPE_GUI, "Text Editor");

#define WIN_W 520
#define WIN_H 360
#define MAX_CHARS 8192
#define FONT_PX 16
#define PATH_MAX 260

#define MENU_H 20
#define TEXT_PAD 3 /* gap between the well's bevel and the first glyph */
#define TAB_W 4    /* tab stops, in columns                            */
#define CARET_W 2
#define CARET_BLINK_MS 530 /* get_ticks() counts milliseconds */

// Colors
#define COLOR_BG 0x00FFFFFF    // White
#define COLOR_FG 0x00000000    // Black
#define COLOR_CARET 0x00000000 // Black

enum {
  MENU_FILE = 1,
};

/* Commands are shared by the File menu and the Ctrl shortcuts, so the two
 * cannot drift apart. */
enum {
  CMD_NONE = 0,
  CMD_NEW,
  CMD_RELOAD,
  CMD_SAVE,
  CMD_SAVE_AS,
  CMD_EXIT,
};

/* Not const: Reload is enabled per frame depending on whether there is a
 * file to reload. font8x8 is fixed width, so spaces line the shortcuts up. */
static struct ui_menu_item file_menu_items[] = {
    {"New            Ctrl+N", CMD_NEW, 1},
    {"Reload         Ctrl+O", CMD_RELOAD, 1},
    {"Save           Ctrl+S", CMD_SAVE, 1},
    {"Save As  Ctrl+Shift+S", CMD_SAVE_AS, 1},
    {"Exit", CMD_EXIT, 1},
};

// Globals
static struct app app;
static struct ui_context ui;
static struct ui_menu_state menu;
static int win_handle    = -1;
static int shift_pressed = 0;
static int alt_pressed   = 0;
static int ctrl_pressed  = 0;

static int mouse_x       = 0;
static int mouse_y       = 0;
static int mouse_buttons = 0;

/* Every input-driven frame repaints the whole window through lib/ui. A
 * caret blink changes one cell-high strip and nothing else, so it records
 * just that strip here and the next draw repaints only the text inside it
 * , no glyph outside the strip is rasterised again. */
static struct gfx_damage damage;
static int full_redraw = 1;

/* Menu choices are acted on outside draw(): Save can block in wm_prompt,
 * and a dialog should not open halfway through painting a frame. */
static int pending_cmd = CMD_NONE;

static int cell_w     = 9;
static int cell_h     = 18;
static int ascent     = 12;
static int descent    = -3;

static char text_buf[MAX_CHARS];
static int text_len = 0;
/* The path is kept rather than an open FILE*: saving reopens with "w" every
 * time, which is what truncates. Holding one handle open across saves would
 * either append a second copy of the text or leave the tail of a longer
 * previous version behind, and could only ever be closed once. */
static char file_path[PATH_MAX];
static int have_file = 0;
static int dirty     = 0;

/* The caret is a byte offset into text_buf, 0..text_len, sitting before
 * text_buf[caret]. Lines and columns are derived from it on demand: the
 * buffer is at most MAX_CHARS, so a scan is cheaper than keeping a line
 * table in step with every edit. */
static int caret    = 0;
static int goal_col = -1; /* column Up/Down aim for; -1 = the caret's own */
static int top_line = 0;  /* first visible line                            */
static int left_col = 0;  /* first visible column                          */
static int caret_on = 1;  /* blink phase                                   */
static long blink_at;     /* get_ticks() of the next phase flip            */

static const char *file_name(void) {
  if (!have_file)
    return "(untitled)";
  const char *slash = strrchr(file_path, '/');
  return slash ? slash + 1 : file_path;
}

/* Point the chrome title at the current file, with a leading '*' while there
 * are unsaved edits. */
static void update_title(void) {
  if (win_handle < 0)
    return;

  char title[64];
  snprintf(title, sizeof(title), "%sNotepad - %s", dirty ? "*" : "", file_name());
  wm_window_set_title(win_handle, title);
}

/* Status line: what file, how big, and whether it needs saving. Replaced
 * wholesale by transient messages from save/load, which the next edit or
 * save overwrites. */
static void update_status(void) {
  if (win_handle < 0)
    return;

  char line[48];
  snprintf(line,
           sizeof(line),
           "%s  %d bytes%s  Ctrl+S save",
           file_name(),
           text_len,
           dirty ? "  (modified)" : "");
  wm_window_set_status(win_handle, line);
}

static void set_status(const char *msg) {
  if (win_handle >= 0)
    wm_window_set_status(win_handle, msg);
}

/* First edit since the last save flips the title to '*'. Gated so ordinary
 * typing doesn't spend an IPC round trip per keystroke. */
static void mark_modified(void) {
  if (dirty)
    return;
  dirty = 1;
  update_title();
  update_status();
}

/* ---------------- Layout ------------------------------------------------ */

static struct gfx_rect menu_rect(void) { return gfx_rect_make(0, 0, app.surface.w, MENU_H); }

static struct gfx_rect well_rect(void) {
  return gfx_rect_make(0, MENU_H, app.surface.w, app.surface.h - MENU_H);
}

/* Inside the well's bevel: the white page. */
static struct gfx_rect page_rect(void) { return gfx_rect_inset(well_rect(), 1); }

/* Where glyph cells start. */
static struct gfx_rect text_rect(void) { return gfx_rect_inset(page_rect(), TEXT_PAD); }

static int visible_rows(void) {
  int rows = text_rect().h / cell_h;
  return rows > 0 ? rows : 1;
}

static int visible_cols(void) {
  int cols = text_rect().w / cell_w;
  return cols > 0 ? cols : 1;
}

/* ---------------- Buffer geometry -------------------------------------- */

static int line_start(int pos) {
  while (pos > 0 && text_buf[pos - 1] != '\n')
    pos--;
  return pos;
}

static int line_end(int pos) {
  while (pos < text_len && text_buf[pos] != '\n')
    pos++;
  return pos;
}

/* Column after drawing `c` at `col`. Tabs jump to the next stop; anything
 * else, printable or not, takes one cell so every byte stays clickable. */
static int advance_col(int col, char c) { return c == '\t' ? (col / TAB_W + 1) * TAB_W : col + 1; }

static int col_of(int pos) {
  int col = 0;
  for (int i = line_start(pos); i < pos; i++)
    col = advance_col(col, text_buf[i]);
  return col;
}

static int line_of(int pos) {
  int line = 0;
  for (int i = 0; i < pos; i++)
    if (text_buf[i] == '\n')
      line++;
  return line;
}

/* Offset of the first byte of `line`. Past the last line it stops at the
 * start of the last one, so callers can clamp by asking for too much. */
static int start_of_line(int line) {
  int pos = 0;
  while (line > 0) {
    int end = line_end(pos);
    if (end >= text_len)
      break;
    pos = end + 1;
    line--;
  }
  return pos;
}

/* Offset in the line starting at `start` whose caret column is nearest
 * `col`. Only a tab can straddle the target; ties go to its left edge. */
static int pos_at_col(int start, int col) {
  int c   = 0;
  int pos = start;
  while (pos < text_len && text_buf[pos] != '\n' && c < col) {
    int next = advance_col(c, text_buf[pos]);
    if (next > col) {
      if (next - col < col - c)
        pos++;
      break;
    }
    c = next;
    pos++;
  }
  return pos;
}

/* ---------------- Caret ------------------------------------------------- */

static struct gfx_rect caret_rect(void) {
  struct gfx_rect t = text_rect();
  int x             = t.x + (col_of(caret) - left_col) * cell_w;
  int y             = t.y + (line_of(caret) - top_line) * cell_h;
  /* Straddle the cell boundary: glyphs are centred in their cells, so the
   * pixel either side of the edge is normally blank. */
  return gfx_rect_make(x - CARET_W / 2, y, CARET_W, cell_h);
}

/* Scroll the view the least distance that brings the caret into it.
 * Horizontal scrolling jumps by a quarter of the width so typing at the
 * right edge does not shift the whole page one column per keystroke. */
static void scroll_to_caret(void) {
  int rows = visible_rows();
  int cols = visible_cols();
  int line = line_of(caret);
  int col  = col_of(caret);
  int step = cols / 4 > 0 ? cols / 4 : 1;

  if (line < top_line)
    top_line = line;
  else if (line >= top_line + rows)
    top_line = line - rows + 1;

  if (col < left_col)
    left_col = col - step > 0 ? col - step : 0;
  else if (col >= left_col + cols)
    left_col = col - cols + 1 + step;
}

/* Moving the caret shows it immediately and restarts the blink, so it is
 * never invisible at the moment the user is looking for it. `keep_goal`
 * is for Up/Down, which must remember the column they started from. */
static void set_caret(int pos, int keep_goal) {
  if (pos < 0)
    pos = 0;
  if (pos > text_len)
    pos = text_len;
  caret = pos;
  if (!keep_goal)
    goal_col = -1;
  scroll_to_caret();
  caret_on = 1;
  blink_at = get_ticks() + CARET_BLINK_MS;
}

static void move_vertical(int lines) {
  if (goal_col < 0)
    goal_col = col_of(caret);

  int start = line_start(caret);
  for (; lines < 0 && start > 0; lines++)
    start = line_start(start - 1);
  for (; lines > 0; lines--) {
    int end = line_end(start);
    if (end >= text_len)
      break;
    start = end + 1;
  }
  set_caret(pos_at_col(start, goal_col), 1);
}

/* Map a point in the window to the nearest caret position. Points outside
 * the text clamp to its edges, so a click in the padding still lands. */
static void place_caret_at(int x, int y) {
  struct gfx_rect t = text_rect();
  int dx            = x - t.x;
  int dy            = y - t.y;
  int col           = left_col + (dx > 0 ? (dx + cell_w / 2) / cell_w : 0);
  int line          = top_line + (dy > 0 ? dy / cell_h : 0);

  set_caret(pos_at_col(start_of_line(line), col), 0);
}

/* ---------------- Editing ----------------------------------------------- */

static int insert_char(char c) {
  if (text_len >= MAX_CHARS - 1)
    return 0;

  memmove(text_buf + caret + 1, text_buf + caret, (size_t)(text_len - caret));
  text_buf[caret] = c;
  text_len++;
  mark_modified();
  set_caret(caret + 1, 0);
  return 1;
}

/* Remove the byte at `pos` and leave the caret where that byte was, which
 * serves both Backspace (pos = caret - 1) and Delete (pos = caret). */
static int delete_at(int pos) {
  if (pos < 0 || pos >= text_len)
    return 0;

  memmove(text_buf + pos, text_buf + pos + 1, (size_t)(text_len - pos - 1));
  text_len--;
  mark_modified();
  set_caret(pos, 0);
  return 1;
}

/* Fresh document or freshly loaded file: caret and view back to the top. */
static void reset_view(void) {
  top_line = 0;
  left_col = 0;
  set_caret(0, 0);
}

/* ---------------- Files ------------------------------------------------- */

/* Read `path` into text_buf. A missing file is not an error , it just means
 * this is a new document that save will create. */
static void load_text_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) {
    printf("notepad: %s not found, starting empty\n", path);
    return;
  }

  /* One bulk read rather than a getc loop: fgetc lives in lib/stdio_extra.o,
   * which only the DOOM build links, and reading byte-at-a-time through an
   * unbuffered FILE is a syscall per character regardless. The MAX_CHARS
   * cap is the buffer bound , a longer file loads its head and says so. */
  size_t got = fread(text_buf, 1, MAX_CHARS, f);
  fclose(f);

  int count = (int)got;
  text_len  = count;
  if (count == MAX_CHARS)
    printf("notepad: %s truncated to %d bytes\n", path, MAX_CHARS);
  else
    printf("notepad: loaded %d bytes from %s\n", count, path);
}

/* Ask heimdall for a path. Returns 1 if file_path now holds one. */
static int prompt_for_path(void) {
  char entered[PATH_MAX];
  int rc = wm_prompt(win_handle, WM_PROMPT_TEXT, "Save as (full path):", entered, sizeof(entered));
  if (rc != WM_PROMPT_OK || !entered[0])
    return 0;

  strncpy(file_path, entered, sizeof(file_path) - 1);
  file_path[sizeof(file_path) - 1] = 0;
  have_file                        = 1;
  return 1;
}

/* save_as: always ask for a path (Ctrl+Shift+S). Otherwise write straight
 * back to the open file, asking only when the buffer has never had a name.
 * A cancelled prompt leaves any existing path untouched, so a cancelled
 * Save As does not detach the document from its file. */
static void save_text(int save_as) {
  if ((save_as || !have_file) && !prompt_for_path()) {
    set_status("Save cancelled");
    return;
  }

  /* "w" maps to O_WRONLY|O_CREAT|O_TRUNC, so the old contents go away
   * before the new ones land. */
  FILE *f = fopen(file_path, "w");
  if (!f) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Cannot write %s", file_name());
    wm_prompt(win_handle, WM_PROMPT_MESSAGE, msg, 0, 0);
    set_status("Save failed");
    printf("notepad: cannot open %s for writing\n", file_path);
    return;
  }

  size_t written = fwrite(text_buf, 1, (size_t)text_len, f);
  fclose(f);

  if (written != (size_t)text_len) {
    wm_prompt(win_handle, WM_PROMPT_MESSAGE, "Short write , disk full?", 0, 0);
    set_status("Save failed");
    printf("notepad: short write to %s (%d of %d bytes)\n", file_path, (int)written, text_len);
    return;
  }

  dirty = 0;
  update_title();

  char msg[48];
  snprintf(msg, sizeof(msg), "Saved %d bytes to %s", text_len, file_name());
  set_status(msg);
  printf("notepad: saved %d bytes to %s\n", text_len, file_path);
}

/* Yes/No/Cancel gate in front of anything that throws the buffer away.
 * Returns 1 when the caller may proceed. Shared by close and new so the two
 * cannot answer the question differently.
 *
 * Yes saves first and only proceeds if that worked , a save that failed or
 * whose Save As prompt was cancelled must not then discard the text it was
 * trying to preserve. */
static int confirm_discard(const char *question) {
  if (!dirty)
    return 1;

  int rc = wm_prompt(win_handle, WM_PROMPT_CONFIRM, question, 0, 0);
  if (rc == WM_PROMPT_CANCEL)
    return 0;
  if (rc == WM_PROMPT_OK) {
    save_text(0);
    return !dirty;
  }
  return 1; /* No , discard the edits */
}

/* Ctrl+N. Empties the buffer and detaches it from any file, so the next
 * save asks where to put it. */
static void new_file(void) {
  if (!confirm_discard("Save changes before the new file?"))
    return;

  text_len     = 0;
  text_buf[0]  = 0;
  file_path[0] = 0;
  have_file    = 0;
  dirty        = 0;
  reset_view();
  update_title();
  update_status();
  printf("notepad: new file\n");
}

/* Reopen == reload. There is no file picker yet, so this rereads the file
 * already open and drops unsaved edits rather than inventing a filename to
 * open. */
static void reload_file(void) {
  if (!have_file) {
    printf("notepad: nothing to reload , launch as 'notepad <path>'\n");
    return;
  }

  text_len = 0;
  load_text_file(file_path);
  dirty = 0;
  reset_view();
  update_title();
  update_status();
}

/* Returns an APP_ACTION_* for the caller to hand back to app_run. */
static int run_command(int cmd) {
  switch (cmd) {
  case CMD_NEW:
    new_file();
    break;
  case CMD_RELOAD:
    reload_file();
    break;
  case CMD_SAVE:
    save_text(0);
    break;
  case CMD_SAVE_AS:
    save_text(1);
    break;
  case CMD_EXIT:
    /* Cancel keeps an unsaved document open. */
    if (confirm_discard("Save changes before closing?"))
      return APP_ACTION_EXIT;
    break;
  default:
    return APP_ACTION_NONE;
  }
  return APP_ACTION_REDRAW;
}

static int run_pending(void) {
  int cmd     = pending_cmd;
  pending_cmd = CMD_NONE;
  return run_command(cmd);
}

/* ---------------- Drawing ----------------------------------------------- */

/* Repaint the page, and whatever text and caret fall inside `area`. */
static void draw_text(struct gfx_surface *s, struct gfx_rect area) {
  struct gfx_rect t    = text_rect();
  struct gfx_rect prev = gfx_clip_push(s, gfx_rect_intersect(page_rect(), area));

  gfx_fill(s, page_rect(), COLOR_BG);

  /* One row past visible_rows() so a partly visible last line still
   * shows; the clip trims it. */
  int rows = visible_rows() + 1;
  int cols = visible_cols() + 1;
  int pos  = start_of_line(top_line);

  for (int row = 0; row < rows; row++) {
    int y   = t.y + row * cell_h;
    int col = 0;

    while (pos < text_len && text_buf[pos] != '\n') {
      char c   = text_buf[pos++];
      int next = advance_col(col, c);

      if (c >= 32 && c <= 126 && col >= left_col && col < left_col + cols) {
        struct gfx_rect cell = gfx_rect_make(t.x + (col - left_col) * cell_w, y, cell_w, cell_h);
        /* Skip the rasteriser for cells the clip would discard anyway ,
         * on a blink that is all but one or two of them. */
        if (!gfx_rect_empty(gfx_rect_intersect(cell, s->clip))) {
          /* Keep glyphs inside their cells. */
          struct gfx_rect outer = gfx_clip_push(s, cell);
          ttf_draw_glyph_cell(
              s, g_sys_font, cell.x, y + ascent, cell_w, (unsigned char)c, FONT_PX, COLOR_FG);
          gfx_clip_set(s, outer);
        }
      }
      col = next;
    }

    if (pos >= text_len)
      break;
    pos++; /* the newline */
  }

  if (caret_on)
    gfx_fill(s, caret_rect(), COLOR_CARET);

  gfx_clip_set(s, prev);
}

/* "Ln 3, Col 7" at the right end of the menu bar. Drawn by the client
 * rather than sent to the status strip, which would cost an IPC round trip
 * on every caret move. */
static void draw_position(struct gfx_surface *s) {
  char pos[32];
  snprintf(pos, sizeof(pos), "Ln %d, Col %d", line_of(caret) + 1, col_of(caret) + 1);

  struct gfx_rect bar = menu_rect();
  gfx_text_box(s,
               gfx_rect_make(bar.x + 60, bar.y, bar.w - 60, bar.h),
               pos,
               ui.theme->text_muted,
               ui.theme->scale,
               6,
               GFX_TEXT_RIGHT);
}

static void draw(struct app *a, void *context) {
  (void)context;
  struct gfx_surface *s = &a->surface;

  if (!full_redraw && gfx_damage_pending(&damage)) {
    draw_text(s, gfx_damage_take(&damage));
    return;
  }
  full_redraw = 0;
  gfx_damage_clear(&damage);

  ui_begin(&ui, s, NULL, mouse_x, mouse_y, mouse_buttons);

  /* A press while a menu is open belongs to the menu, whether it picks an
   * item or only dismisses the popup, so it must not also move the caret.
   * Sampled before the menu bar runs, which is what closes it. */
  int pressed = ui.down && !ui.was_down;
  if (pressed && !menu.open && gfx_rect_contains(page_rect(), ui.mx, ui.my))
    place_caret_at(ui.mx, ui.my);

  ui_panel(&ui, menu_rect());
  ui_well(&ui, well_rect());
  draw_text(s, gfx_surface_bounds(s));
  draw_position(s);

  /* Last, so the popup paints over the text. */
  file_menu_items[1].enabled = have_file;
  int cmd                    = ui_menu_bar_item(&ui,
                                 &menu,
                                 MENU_FILE,
                                 gfx_rect_make(2, 1, 40, MENU_H - 2),
                                 "File",
                                 file_menu_items,
                                 (int)(sizeof(file_menu_items) / sizeof(file_menu_items[0])));
  if (cmd)
    pending_cmd = cmd;

  ui_end(&ui);
}

/* ---------------- Input ------------------------------------------------- */

/* Returns 1 when the key changed something on screen. */
static int handle_key_down(int keycode) {
  /* One chain, so a modifier keypress can't also fall through into
   * an editing case, and Ctrl+<key> is always a command rather than
   * text: Ctrl+Enter used to insert a newline. */
  if (keycode == KEY_LEFTSHIFT || keycode == KEY_RIGHTSHIFT) {
    shift_pressed = 1;
  } else if (keycode == KEY_LEFTALT || keycode == KEY_RIGHTALT) {
    alt_pressed = 1;
  } else if (keycode == KEY_LEFTCTRL || keycode == KEY_RIGHTCTRL) {
    ctrl_pressed = 1;
  }
  // Commands
  else if (ctrl_pressed) {
    if (keycode == KEY_S) {
      pending_cmd = shift_pressed ? CMD_SAVE_AS : CMD_SAVE;
    } else if (keycode == KEY_O && !shift_pressed) {
      pending_cmd = CMD_RELOAD;
    } else if (keycode == KEY_N && !shift_pressed) {
      pending_cmd = CMD_NEW;
    } else if (keycode == KEY_HOME) {
      set_caret(0, 0);
      return 1;
    } else if (keycode == KEY_END) {
      set_caret(text_len, 0);
      return 1;
    }
  }
  // Caret movement
  else if (keycode == KEY_LEFT) {
    set_caret(caret - 1, 0);
    return 1;
  } else if (keycode == KEY_RIGHT) {
    set_caret(caret + 1, 0);
    return 1;
  } else if (keycode == KEY_UP) {
    move_vertical(-1);
    return 1;
  } else if (keycode == KEY_DOWN) {
    move_vertical(1);
    return 1;
  } else if (keycode == KEY_PAGEUP) {
    move_vertical(-visible_rows());
    return 1;
  } else if (keycode == KEY_PAGEDOWN) {
    move_vertical(visible_rows());
    return 1;
  } else if (keycode == KEY_HOME) {
    set_caret(line_start(caret), 0);
    return 1;
  } else if (keycode == KEY_END) {
    set_caret(line_end(caret), 0);
    return 1;
  }
  // Editing
  else if (keycode == KEY_ENTER || keycode == KEY_KPENTER) {
    return insert_char('\n');
  } else if (keycode == KEY_TAB) {
    return insert_char('\t');
  } else if (keycode == KEY_BACKSPACE) {
    return delete_at(caret - 1);
  } else if (keycode == KEY_DELETE) {
    return delete_at(caret);
  } else {
    char c = keymap_to_ascii(keycode, shift_pressed);

    if (c >= 32 && c <= 126)
      return insert_char(c);
  }
  return 0;
}

static void handle_key_up(int keycode) {
  if (keycode == KEY_LEFTSHIFT || keycode == KEY_RIGHTSHIFT) {
    shift_pressed = 0;
  } else if (keycode == KEY_LEFTALT || keycode == KEY_RIGHTALT) {
    alt_pressed = 0;
  } else if (keycode == KEY_LEFTCTRL || keycode == KEY_RIGHTCTRL) {
    ctrl_pressed = 0;
  }
}

/* Hover only changes the picture over the menu bar or an open popup, so a
 * pointer wandering over the text costs no frames. */
static int pointer_matters(int y) { return menu.open || y < MENU_H || mouse_y < MENU_H; }

static int on_event(struct app *a, const struct wm_event *event, void *context) {
  (void)a;
  (void)context;
  int action = APP_ACTION_NONE;

  switch (event->type) {
  case WM_EV_QUIT:
    /* The owner must destroy its window after WM_EV_QUIT; returning
     * anything but NONE or EXIT keeps it open. */
    action = run_command(CMD_EXIT);
    if (action & APP_ACTION_REDRAW)
      full_redraw = 1;
    return action;

  case WM_EV_RESIZE:
    scroll_to_caret();
    action = APP_ACTION_REDRAW;
    break;

  case WM_EV_MOUSE_MOVE:
    if (pointer_matters(event->y))
      action = APP_ACTION_REDRAW;
    mouse_x = event->x;
    mouse_y = event->y;
    break;

  case WM_EV_MOUSE_DOWN:
    mouse_x = event->x;
    mouse_y = event->y;
    mouse_buttons |= event->param;
    action = APP_ACTION_REDRAW;
    break;

  case WM_EV_MOUSE_UP:
    mouse_x = event->x;
    mouse_y = event->y;
    mouse_buttons &= ~event->param;
    action = APP_ACTION_REDRAW;
    break;

  case WM_EV_KEY_DOWN:
    if (handle_key_down(event->param))
      action = APP_ACTION_REDRAW;
    break;

  case WM_EV_KEY_UP:
    handle_key_up(event->param);
    break;

  default:
    break;
  }

  /* A shortcut or a menu pick from the previous frame. Run here as well as
   * from idle so a steady stream of mouse moves cannot starve it. */
  action |= run_pending();

  if (action & APP_ACTION_REDRAW)
    full_redraw = 1;
  return action;
}

static int on_idle(struct app *a, void *context) {
  (void)a;
  (void)context;

  int action = run_pending();
  if (action & APP_ACTION_REDRAW)
    full_redraw = 1;

  /* While a menu is open the caret holds still: a partial repaint of the
   * text would cut through the popup lying over it. */
  long now = get_ticks();
  if (!menu.open && now >= blink_at) {
    caret_on = !caret_on;
    blink_at = now + CARET_BLINK_MS;
    if (gfx_damage_add(&damage, caret_rect(), page_rect()))
      action |= APP_ACTION_REDRAW;
  }
  return action;
}

int main(int argc, char **argv) {
  if (argc > 1 && argv[1] && argv[1][0]) {
    strncpy(file_path, argv[1], sizeof(file_path) - 1);
    file_path[sizeof(file_path) - 1] = 0;
    have_file                        = 1;
  }

  /* Font state is process-local. */
  ttf_init_font();
  if (g_sys_font) {
    cell_w = ttf_cell_width(g_sys_font, FONT_PX);
    if (cell_w < 4)
      cell_w = 8;
    if (cell_w > 20)
      cell_w = 20;
    cell_h = FONT_PX + 3;
    ttf_vmetrics(g_sys_font, FONT_PX, &ascent, &descent, 0);
    if (ascent <= 0)
      ascent = (FONT_PX * 3) / 4;
    printf("notepad: TTF loaded cell=%dx%d\n", cell_w, cell_h);
  } else {
    printf("notepad: TTF failed to load!\n");
    return 1;
  }

  if (app_open_ex(&app, WIN_W, WIN_H, "Notepad", WM_CREATE_STATUSBAR) != 0) {
    printf("notepad: Failed to get window\n");
    return 1;
  }
  win_handle = app.window.handle;

  /* Load after window creation so errors can be displayed. */
  if (have_file)
    load_text_file(file_path);
  reset_view();
  update_title();
  update_status();

  const struct app_callbacks callbacks = {
      .event = on_event,
      .draw  = draw,
      .idle  = on_idle,
  };
  /* app_run destroys the window before it returns. */
  app_run(&app, &callbacks, 0);

  printf("notepad: exit\n");
  return 0;
}
