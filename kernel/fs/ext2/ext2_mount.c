#include "utilities/log.h"
#include <fs/ext2/ext2_internal.h>
#include <fs/probe.h>
#include <memory/heap.h>
#include <utilities/string.h>

void *ext2_bytes(struct ext2_fs *fs, u64 offset, usize length) {
  if (!fs || offset > fs->image_size || length > fs->image_size - offset)
    return 0;
  if (fs->lazy && ext2_lazy_ensure(fs->lazy, offset, length)) {
    fs->io_failed = 1;
    return 0;
  }
  return fs->image + (usize)offset;
}

void *ext2_block(struct ext2_fs *fs, u32 block_number) {
  if (!fs || !block_number || block_number >= fs->superblock->blocks_count)
    return 0;
  return ext2_bytes(fs, (u64)block_number * fs->block_size, fs->block_size);
}

static int ext2_probe_image(const void *image, usize size) { return fs_probe_is_ext(image, size); }

/* Shared by RAM images (lazy == NULL) and device mounts. With a lazy
 * image, everything read here before the ext2_fs exists , the probe
 * header, the superblock, the group descriptors , is made resident first;
 * from then on ext2_bytes does it. */
static int mount_common(struct vfs_superblock *super, u8 *image, usize size,
                        struct ext2_lazy *lazy) {
  if (lazy && ext2_lazy_ensure(lazy, 0, size < 2048 ? size : 2048))
    return -1;
  if (!ext2_probe_image(image, size))
    return -1;
  struct ext2_superblock *superblock =
      (struct ext2_superblock *)((u8 *)image + EXT_SUPERBLOCK_OFFSET);
  log_write_fmt(FILESYS, LOG_DEBUG,
                "EXT2: revision=%u block_size=%u inode_size=%u",
                superblock->revision_level,
                1024u << superblock->log_block_size,
                superblock->inode_size);

  log_write_fmt(FILESYS, LOG_DEBUG,
                "EXT2: blocks=%u first_data_block=%u image_size=%llu",
                superblock->blocks_count,
                superblock->first_data_block,
                (unsigned long long)size);

  log_write_fmt(FILESYS, LOG_DEBUG,
                "EXT2: compat=0x%x incompat=0x%x ro_compat=0x%x",
                superblock->feature_compat,
                superblock->feature_incompat,
                superblock->feature_ro_compat);
  if (superblock->revision_level > 1 || superblock->log_block_size > 2 ||
      superblock->blocks_per_group == 0 || superblock->inodes_per_group == 0 ||
      superblock->blocks_count <= superblock->first_data_block ||
      (superblock->feature_compat & EXT2_FEATURE_COMPAT_HAS_JOURNAL) ||
      (superblock->feature_incompat & ~EXT2_FEATURE_INCOMPAT_FILETYPE) ||
      (superblock->feature_ro_compat & ~0x0003u))
    return -1;

  u32 block_size = 1024u << superblock->log_block_size;
  if (superblock->first_data_block != (block_size == 1024 ? 1u : 0u) ||
      superblock->log_fragment_size != (i32)superblock->log_block_size ||
      superblock->blocks_per_group > block_size * 8u ||
      superblock->inodes_per_group > block_size * 8u)
    return -1;
  u64 volume_size = (u64)superblock->blocks_count * block_size;
  if (volume_size > size) {
      log_write_fmt(FILESYS, LOG_ERROR,
                    "EXT2: volume_size=%llu exceeds image_size=%llu",
                    (unsigned long long)volume_size,
                    (unsigned long long)size);
      return -1;
  }
  u32 data_blocks = superblock->blocks_count - superblock->first_data_block;
  u32 group_count = (data_blocks + superblock->blocks_per_group - 1) / superblock->blocks_per_group;
  u32 inode_groups =
      (superblock->inodes_count + superblock->inodes_per_group - 1) / superblock->inodes_per_group;
  if (group_count == 0 || inode_groups != group_count)
    return -1;

  u32 inode_size = superblock->revision_level == 0 ? 128u : superblock->inode_size;
  if (inode_size < sizeof(struct ext2_inode) || inode_size > block_size || (inode_size & 3u))
    return -1;

  u64 descriptor_offset  = (u64)(superblock->first_data_block + 1) * block_size;
  usize descriptor_bytes = (usize)group_count * sizeof(struct ext2_group_descriptor);
  if (lazy && descriptor_offset <= size && descriptor_bytes <= size - descriptor_offset &&
      ext2_lazy_ensure(lazy, descriptor_offset, descriptor_bytes))
    return -1;
  struct ext2_group_descriptor *groups =
      descriptor_offset <= size && descriptor_bytes <= size - descriptor_offset
          ? (struct ext2_group_descriptor *)((u8 *)image + descriptor_offset)
          : 0;
  if (!groups)
    return -1;

  struct ext2_fs *fs = kmalloc(sizeof(*fs));
  if (!fs)
    return -1;
  memset(fs, 0, sizeof(*fs));
  fs->image              = image;
  fs->image_size         = size;
  fs->superblock         = superblock;
  fs->groups             = groups;
  fs->block_size         = block_size;
  fs->inode_size         = inode_size;
  fs->group_count        = group_count;
  fs->pointers_per_block = block_size / sizeof(u32);
  fs->lazy               = lazy;

  u32 inode_table_blocks =
      (u32)(((u64)superblock->inodes_per_group * inode_size + block_size - 1) / block_size);
  for (u32 group = 0; group < group_count; group++) {
    const struct ext2_group_descriptor *descriptor = &groups[group];
    if (!ext2_block(fs, descriptor->block_bitmap) || !ext2_block(fs, descriptor->inode_bitmap) ||
        !descriptor->inode_table || descriptor->inode_table >= superblock->blocks_count ||
        inode_table_blocks > superblock->blocks_count - descriptor->inode_table) {
      kfree(fs);
      return -1;
    }
  }

  struct ext2_inode *root = ext2_inode_get(fs, EXT2_ROOT_INODE);
  if (!root || (root->mode & EXT2_S_IFMT) != EXT2_S_IFDIR) {
    kfree(fs);
    return -1;
  }
  super->private_data = fs;
  super->root         = ext2_vfs_inode(super, EXT2_ROOT_INODE);
  return super->root ? 0 : -1;
}

int ext2_mount_image(struct vfs_superblock *super, void *image, usize size) {
  return mount_common(super, image, size, 0);
}

int ext2_mount_lazy(struct vfs_superblock *super, struct ext2_lazy *lazy, u8 *image, usize size) {
  return mount_common(super, image, size, lazy);
}

static void ext2_unmount_image(struct vfs_superblock *super) {
  struct ext2_fs *fs = super->private_data;
  if (!fs)
    return;
  if (fs->dirty_blocks)
    kfree(fs->dirty_blocks);
  if (fs->lazy)
    ext2_lazy_release(fs->lazy);
  else if (fs->owns_image)
    kfree(fs->image);
  kfree(fs);
}

/* The superblock counters are kept current by every allocation, and the
 * superblock is resident for the life of the mount. */
static int ext2_statfs(struct vfs_superblock *super, u64 *total, u64 *free) {
  struct ext2_fs *fs = super->private_data;
  if (!fs || !fs->superblock)
    return -1;
  *total = (u64)fs->superblock->blocks_count * fs->block_size;
  *free  = (u64)fs->superblock->free_blocks_count * fs->block_size;
  return 0;
}

const struct vfs_filesystem ext2_filesystem = {
    .name    = "ext2",
    .probe   = ext2_probe_image,
    .mount   = ext2_mount_image,
    .attach  = ext2_mount_block,
    .sync    = ext2_sync_super,
    .statfs  = ext2_statfs,
    .unmount = ext2_unmount_image,
};
