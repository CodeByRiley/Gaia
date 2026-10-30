/* Device-backed image cache and explicit dirty-block writeback. The disk is
 * marked unclean BEFORE changing blocks, and clean only after a cache barrier.
 * This detects interrupted writes; it is not a journal or atomic rollback. */
#include "ext2.h"
#include "ext2_internal.h"
#include <fs/probe.h>
#include <memory/heap.h>
#include <utilities/string.h>

#define EXT2_VALID_FS 1u
#define EXT2_PAGE 4096u
#define EXT2_PAGE_SECTORS (EXT2_PAGE / BLOCK_SECTOR_SIZE)

/* --- On-demand image ------------------------------------------------------
 *
 * A device mount used to copy the whole volume into one kmalloc, which
 * capped volumes at 128 MiB and spent the volume's size in RAM whether or
 * not a byte of it was read. Instead each mount reserves a window of kernel
 * address space the size of the volume and fills it a page at a time, when
 * ext2_bytes first covers the page. The driver keeps raw pointers into its
 * image , an open file's inode is one , so pages are never evicted:
 * resident memory is the set of pages ever touched, at most the volume.
 *
 * Windows sit in their own kernel PML4 slot, 0xFFFFB000_00000000, which
 * vmm_init pre-creates like every kernel-half slot, so a page mapped here
 * is visible in every process. One window per possible mount. The host
 * test build has no VMM; it backs the window with calloc and keeps the
 * same load-on-first-touch logic, so ext2_device_test exercises it. */
#define EXT2_WINDOW_BASE 0xFFFFB00000000000ULL
#define EXT2_WINDOW_SIZE (64ULL << 30) /* largest volume a mount accepts */
#define EXT2_WINDOWS VFS_MAX_MOUNTS

struct ext2_lazy {
  struct block_device device;
  u8 *base;
  u64 size;
  u64 pages;
  u8 *loaded; /* one bit per page */
  int window;
};

#ifdef VFS_HOST_TEST
#include <stdlib.h>
static u8 *window_reserve(int *slot, u64 size) {
  *slot = 0;
  return calloc(1, (size_t)size);
}
static int window_map(struct ext2_lazy *lazy, u64 page) {
  (void)lazy;
  (void)page;
  return 0;
}
static void window_unmap(struct ext2_lazy *lazy, u64 page) {
  (void)lazy;
  (void)page;
}
static void window_free(struct ext2_lazy *lazy) { free(lazy->base); }
#else
#include <memory/hhdm.h>
#include <memory/pmm.h>
#include <memory/vmm.h>
extern u64 *kernel_pml4;
static u8 windows_used[EXT2_WINDOWS];

static u8 *window_reserve(int *slot, u64 size) {
  if (size > EXT2_WINDOW_SIZE)
    return 0;
  for (int i = 0; i < EXT2_WINDOWS; i++) {
    if (!windows_used[i]) {
      windows_used[i] = 1;
      *slot           = i;
      return (u8 *)(uintptr_t)(EXT2_WINDOW_BASE + (u64)i * EXT2_WINDOW_SIZE);
    }
  }
  return 0;
}
static int window_map(struct ext2_lazy *lazy, u64 page) {
  u64 phys = pmm_alloc_frame();
  if (!phys)
    return -1;
  u64 va = (u64)(uintptr_t)lazy->base + page * EXT2_PAGE;
  if (vmm_map_in(kernel_pml4, va, phys, VMM_PRESENT | VMM_WRITE | VMM_NX)) {
    pmm_free_frame(phys);
    return -1;
  }
  return 0;
}
static void window_unmap(struct ext2_lazy *lazy, u64 page) {
  u64 va    = (u64)(uintptr_t)lazy->base + page * EXT2_PAGE;
  u64 entry = vmm_entry_in(kernel_pml4, va);
  if (!entry)
    return;
  vmm_unmap_in(kernel_pml4, va);
  pmm_free_frame(entry & VMM_ADDR_MASK);
}
static void window_free(struct ext2_lazy *lazy) { windows_used[lazy->window] = 0; }
#endif

static int page_loaded(const struct ext2_lazy *lazy, u64 page) {
  return (lazy->loaded[page / 8] >> (page % 8)) & 1;
}

/* Read one page of the volume into the window. The last page of a volume
 * whose size is not a page multiple is read short and its tail zeroed. */
static int load_page(struct ext2_lazy *lazy, u64 page) {
  if (window_map(lazy, page))
    return -1;
  u8 *at        = lazy->base + page * EXT2_PAGE;
  u64 first     = page * EXT2_PAGE_SECTORS;
  u64 remaining = lazy->size / BLOCK_SECTOR_SIZE - first;
  u32 count     = remaining < EXT2_PAGE_SECTORS ? (u32)remaining : EXT2_PAGE_SECTORS;
  if (count < EXT2_PAGE_SECTORS)
    memset(at + (usize)count * BLOCK_SECTOR_SIZE, 0, (EXT2_PAGE_SECTORS - count) * BLOCK_SECTOR_SIZE);
  if (block_read(&lazy->device, first, count, at)) {
    window_unmap(lazy, page);
    return -1;
  }
  lazy->loaded[page / 8] |= (u8)(1u << (page % 8));
  return 0;
}

int ext2_lazy_ensure(struct ext2_lazy *lazy, u64 offset, usize length) {
  if (!lazy || offset > lazy->size || length > lazy->size - offset)
    return -1;
  if (!length)
    return 0;
  for (u64 page = offset / EXT2_PAGE; page <= (offset + length - 1) / EXT2_PAGE; page++)
    if (!page_loaded(lazy, page) && load_page(lazy, page))
      return -1;
  return 0;
}

void ext2_lazy_release(struct ext2_lazy *lazy) {
  if (!lazy)
    return;
  for (u64 page = 0; page < lazy->pages; page++)
    if (page_loaded(lazy, page))
      window_unmap(lazy, page);
  window_free(lazy);
  kfree(lazy->loaded);
  kfree(lazy);
}

static struct ext2_lazy *lazy_create(const struct block_device *device, u64 size) {
  struct ext2_lazy *lazy = kmalloc(sizeof(*lazy));
  if (!lazy)
    return 0;
  memset(lazy, 0, sizeof(*lazy));
  lazy->device = *device;
  lazy->size   = size;
  lazy->pages  = (size + EXT2_PAGE - 1) / EXT2_PAGE;
  lazy->loaded = kmalloc((usize)((lazy->pages + 7) / 8));
  lazy->base   = lazy->loaded ? window_reserve(&lazy->window, size) : 0;
  if (!lazy->base) {
    if (lazy->loaded)
      kfree(lazy->loaded);
    kfree(lazy);
    return 0;
  }
  memset(lazy->loaded, 0, (usize)((lazy->pages + 7) / 8));
  return lazy;
}

int ext2_mount_block(struct vfs_superblock *super, void *context) {
  const struct block_device *device = context;
  u8 header[2048];
  if (!device || !device->write || !device->flush ||
      block_read(device, 0, sizeof(header) / BLOCK_SECTOR_SIZE, header) ||
      !fs_probe_is_ext(header, sizeof(header)))
    return -1;
  struct ext2_superblock *disk = (void *)(header + EXT_SUPERBLOCK_OFFSET);
  if (disk->log_block_size > 2 || disk->state != EXT2_VALID_FS)
    return -1;
  u64 size = (u64)disk->blocks_count * (1024u << disk->log_block_size);
  if (size < sizeof(header) || size > EXT2_WINDOW_SIZE || size / BLOCK_SECTOR_SIZE > device->sectors)
    return -1;
  struct ext2_lazy *lazy = lazy_create(device, size);
  if (!lazy)
    return -1;
  int result         = ext2_mount_lazy(super, lazy, lazy->base, (usize)size);
  struct ext2_fs *fs = super->private_data;
  if (!fs) {
    ext2_lazy_release(lazy);
    return -1;
  }
  /* From here the mount owns the image: unmount releases it. */
  if (result)
    return result;
  fs->device        = *device;
  usize bitmap_size = ((usize)fs->superblock->blocks_count + 7) / 8;
  fs->dirty_blocks  = kmalloc(bitmap_size);
  if (!fs->dirty_blocks)
    return -1;
  memset(fs->dirty_blocks, 0, bitmap_size);
  return 0;
}

int ext2_mount_device(const char *path, const struct block_device *device) {
  return vfs_attach(path, "ext2", (void *)device);
}

void ext2_dirty(struct ext2_fs *fs, const void *address, usize length) {
  if (!fs || !fs->dirty_blocks || !address || !length)
    return;
  uintptr_t base = (uintptr_t)fs->image, at = (uintptr_t)address;
  if (at < base || at - base >= fs->image_size || length > fs->image_size - (at - base)) {
    fs->io_failed = 1;
    return;
  }
  u32 first = (u32)((at - base) / fs->block_size);
  u32 last  = (u32)((at - base + length - 1) / fs->block_size);
  for (u32 block = first; block <= last; block++)
    fs->dirty_blocks[block / 8] |= (u8)(1u << (block % 8));
}

static int write_block(struct ext2_fs *fs, u32 number) {
  u32 sectors = fs->block_size / BLOCK_SECTOR_SIZE;
  return block_write(
      &fs->device, (u64)number * sectors, sectors, fs->image + (usize)number * fs->block_size);
}

int ext2_sync(struct ext2_fs *fs) {
  if (!fs)
    return -1;
  if (!fs->dirty_blocks)
    return 0;
  u32 blocks = fs->superblock->blocks_count;
  int dirty  = 0;
  for (u32 i = 0; i < (blocks + 7) / 8; i++)
    dirty |= fs->dirty_blocks[i];
  if (!dirty)
    return fs->io_failed ? -1 : 0;
  u32 super_block = EXT_SUPERBLOCK_OFFSET / fs->block_size;
  fs->superblock->state &= ~EXT2_VALID_FS;
  if (write_block(fs, super_block) || block_flush(&fs->device))
    goto failed;
  for (u32 number = 0; number < blocks; number++)
    if ((fs->dirty_blocks[number / 8] & (1u << (number % 8))) && write_block(fs, number))
      goto failed;
  if (block_flush(&fs->device))
    goto failed;
  fs->superblock->state |= EXT2_VALID_FS;
  if (write_block(fs, super_block) || block_flush(&fs->device))
    goto failed;
  memset(fs->dirty_blocks, 0, ((usize)blocks + 7) / 8);
  fs->io_failed = 0;
  return 0;
failed:
  fs->superblock->state &= ~EXT2_VALID_FS;
  fs->io_failed = 1; /* Keep dirty information; reject subsequent mutation. */
  return -1;
}

int ext2_sync_super(struct vfs_superblock *super) { return ext2_sync(super->private_data); }
