#ifndef __LINUX_KERNEL__
#define __LINUX_KERNEL__
#endif

#include <linux/backing-dev.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/fscrypt.h>
#include <linux/fsnotify_backend.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/security.h>

#include "fs.h"

static struct dentry *bbfs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    return mount_bdev(fs_type, flags, dev_name, data, bbfs_fill_super);
}

static struct file_system_type fs_type = {
    .owner = THIS_MODULE,
    .fs_flags = FS_REQUIRES_DEV | FS_ALLOW_IDMAP,
    .name = "bbfs",
    .mount = bbfs_mount,
    .kill_sb = kill_block_super,
};

static int __init bbfs_init(void) {
    int ret = bbfs_init_inode_cache();
    if (ret)
        return ret;
    return register_filesystem(&fs_type);
}

static void __exit bbfs_exit(void) {
    unregister_filesystem(&fs_type);
    bbfs_destroy_inode_cache();
}

module_init(bbfs_init);
module_exit(bbfs_exit);

MODULE_LICENSE("GPL");
