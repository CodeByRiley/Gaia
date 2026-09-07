/* Component lookup and namespace mutations. No disk-format knowledge. */
#include "internal.h"

int vfs_path_length(const char *path, size_t *length) {
    if (!path || !length || path[0] != '/') return -EINVAL;
    for (size_t i = 1; i < VFS_PATH_MAX; i++) {
        if (!path[i]) { *length = i; return 0; }
    }
    return -ENAMETOOLONG;
}

static void pop_entry(struct vfs_path *path) {
    struct vfs_dentry *entry = path->entry;
    path->entry = entry->parent;
    vfs_inode_put(entry->inode);
    kfree(entry);
    path->name[path->entry ? path->entry->path_length : 0] = 0;
}

void vfs_path_put(struct vfs_path *path) {
    vfs_assert_locked();
    while (path->entry) pop_entry(path);
}

/* Consumes inode even on allocation failure. */
static int push_entry(struct vfs_path *path, struct vfs_inode *inode,
                       const struct vfs_mount *mount) {
    struct vfs_dentry *entry = kmalloc(sizeof(*entry));
    if (!entry) { vfs_inode_put(inode); return -ENOMEM; }
    *entry = (struct vfs_dentry){ .parent = path->entry, .inode = inode,
        .mount = mount, .path_length = strlen(path->name) };
    path->entry = entry;
    return 0;
}

int vfs_lookup(const char *path, struct vfs_path *out) {
    vfs_assert_locked();
    if (!out) return -EINVAL;
    memset(out, 0, sizeof(*out));
    size_t length;
    struct vfs_mount *root = vfs_find_mount("/");
    if (!root) return -ENODEV;
    int result = vfs_path_length(path, &length);
    if (result) return result;
    strcpy(out->name, "/");
    result = push_entry(out, vfs_inode_ref(root->super.root), root);
    if (result) return result;
    for (size_t i = 1; i < length;) {
        if (path[i] == '/') { i++; continue; }
        struct vfs_inode *dir = out->entry->inode;
        if (dir->type != VFS_NODE_DIRECTORY) { result = -ENOTDIR; goto fail; }
        size_t start = i;
        while (i < length && path[i] != '/') i++;
        size_t count = i - start;
        if (count > VFS_NAME_MAX) { result = -ENAMETOOLONG; goto fail; }
        char name[VFS_NAME_MAX + 1];
        memcpy(name, path + start, count);
        name[count] = 0;
        if (!strcmp(name, ".")) continue;
        if (!strcmp(name, "..")) {
            if (out->entry->parent) pop_entry(out);
            continue;
        }
        size_t at = out->entry->path_length;
        if (at > 1) out->name[at++] = '/';
        if (count >= sizeof(out->name) - at) { result = -ENAMETOOLONG; goto fail; }
        memcpy(out->name + at, name, count + 1);
        const struct vfs_mount *mount = vfs_find_mount(out->name);
        struct vfs_inode *child = 0;
        if (mount) {
            child = vfs_inode_ref(mount->super.root);
        } else {
            mount = out->entry->mount;
            if (!dir->operations->lookup) { result = -EIO; goto fail; }
            if (dir->operations->lookup(dir, name, &child)) {
                result = -ENOENT;
                goto fail;
            }
        }
        if (!child) { result = -EIO; goto fail; }
        result = push_entry(out, child, mount);
        if (result) goto fail;
    }
    if (path[length - 1] == '/' && out->entry->inode->type != VFS_NODE_DIRECTORY)
        { result = -ENOTDIR; goto fail; }
    return 0;
fail:
    vfs_path_put(out);
    return result;
}

int vfs_lookup_parent(const char *path, struct vfs_path *parent,
                      char name[VFS_NAME_MAX + 1], int *trailing_slash) {
    vfs_assert_locked();
    size_t length;
    memset(parent, 0, sizeof(*parent));
    int result = vfs_path_length(path, &length);
    if (result) return result;
    *trailing_slash = path[length - 1] == '/';
    while (length > 1 && path[length - 1] == '/') length--;
    size_t start = length;
    while (start && path[start - 1] != '/') start--;
    size_t count = length - start;
    if (!count || count > VFS_NAME_MAX) return -EINVAL;
    memcpy(name, path + start, count);
    name[count] = 0;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return -EINVAL;
    char prefix[VFS_PATH_MAX];
    memcpy(prefix, path, start);
    prefix[start] = 0;
    result = vfs_lookup(prefix, parent);
    if (result) return result;
    if (parent->entry->inode->type != VFS_NODE_DIRECTORY) { result = -ENOTDIR; goto fail; }
    size_t at = strlen(parent->name);
    if (at > 1) parent->name[at++] = '/';
    if (count >= sizeof(parent->name) - at) { result = -ENAMETOOLONG; goto fail; }
    memcpy(parent->name + at, name, count + 1);
    /* Mutating a mountpoint (or its ancestor) would orphan a mount. */
    if (vfs_mount_contains(parent->name)) { result = -EBUSY; goto fail; }
    return 0;
fail:
    vfs_path_put(parent);
    return result;
}

int vfs_getattr(struct vfs_inode *inode, struct vfs_stat *out) {
    vfs_assert_locked();
    if (!inode || !out) return -EINVAL;
    if (!inode->operations || !inode->operations->getattr) return -EIO;
    memset(out, 0, sizeof(*out));
    return inode->operations->getattr(inode, out) ? -EIO : 0;
}

int vfs_stat(const char *path, struct vfs_stat *out) {
    VFS_GUARD();
    struct vfs_path found;
    if (!out) return -EINVAL;
    int result = vfs_lookup(path, &found);
    if (result) return result;
    result = vfs_getattr(found.entry->inode, out);
    vfs_path_put(&found);
    return result;
}

enum name_operation { CREATE_DIRECTORY, REMOVE_FILE, REMOVE_DIRECTORY };

static int change_name(const char *path, enum name_operation operation) {
    VFS_GUARD();
    struct vfs_path parent;
    char name[VFS_NAME_MAX + 1];
    int trailing;
    int result = vfs_lookup_parent(path, &parent, name, &trailing);
    if (result) return result;
    struct vfs_inode *dir = parent.entry->inode;
    const struct vfs_inode_operations *ops = dir->operations;
    struct vfs_inode *target = 0;
    result = -EROFS;
    if (operation == CREATE_DIRECTORY) {
        if (ops->mkdir) result = ops->mkdir(dir, name) ? -EIO : 0;
    } else if (ops->lookup && !ops->lookup(dir, name, &target)) {
        /* Defer POSIX unlink-open semantics until backends support orphaned
         * storage. Never let an open handle point at freed/reused disk data. */
        if (!target) result = -EIO;
        else if (target->references != 1) result = -EBUSY;
        else if (operation == REMOVE_FILE) {
            if (trailing || target->type == VFS_NODE_DIRECTORY) result = -EISDIR;
            else if (target->type != VFS_NODE_FILE) result = -EINVAL;
            else if (ops->unlink) result = ops->unlink(dir, name) ? -EIO : 0;
        } else {
            if (target->type != VFS_NODE_DIRECTORY) result = -ENOTDIR;
            else if (ops->rmdir) result = ops->rmdir(dir, name) ? -EIO : 0;
        }
    } else {
        result = -ENOENT;
    }
    vfs_inode_put(target);
    vfs_path_put(&parent);
    return result;
}

int vfs_mkdir(const char *path) { return change_name(path, CREATE_DIRECTORY); }
int vfs_unlink(const char *path) { return change_name(path, REMOVE_FILE); }
int vfs_rmdir(const char *path) { return change_name(path, REMOVE_DIRECTORY); }
