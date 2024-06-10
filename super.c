#ifndef __LINUX_KERNEL__
#define __LINUX_KERNEL__
#endif

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/statfs.h>

#include "fs.h"

static struct kmem_cache *bbfs_inode_cache;

int bbfs_init_inode_cache(void) {
    bbfs_inode_cache = kmem_cache_create_usercopy("bbfs_cache", sizeof(struct bbfs_inode_info), 0, 0, 0,
                                                  sizeof(struct bbfs_inode_info), NULL);
    if (!bbfs_inode_cache) {
        return -ENOMEM;
    }
    return 0;
}

void bbfs_destroy_inode_cache(void) { kmem_cache_destroy(bbfs_inode_cache); }

static struct inode *bbfs_alloc_inode(struct super_block *sb) {
    struct bbfs_inode_info *ci = kmem_cache_alloc(bbfs_inode_cache, GFP_KERNEL);
    if (!ci) {
        return NULL;
    }
    inode_init_once(&ci->vfs_inode);
    return &ci->vfs_inode;
}

static void bbfs_destroy_inode(struct inode *inode) {
    struct bbfs_inode_info *ci = BBFS_INODE(inode);
    kmem_cache_free(bbfs_inode_cache, ci);
}

static int bbfs_write_inode(struct inode *inode, struct writeback_control *wbc) {
    struct super_block *sb = inode->i_sb;
    struct bbfs_sb_info *sbi = BBFS_SB(sb);
    struct bbfs_inode_info *ci = BBFS_INODE(inode);
    ci->disk_inode.i_mode = inode->i_mode;
    ci->disk_inode.i_uid = i_uid_read(inode);
    ci->disk_inode.i_gid = i_gid_read(inode);
    ci->disk_inode.i_size = inode->i_size;
    ci->disk_inode.i_nlink = inode->i_nlink;

    ci->disk_inode.i_ctime_sec = inode_get_ctime_sec(inode);
    ci->disk_inode.i_ctime_nsec = inode_get_ctime_nsec(inode);
    ci->disk_inode.i_atime_sec = inode_get_atime_sec(inode);
    ci->disk_inode.i_atime_nsec = inode_get_atime_nsec(inode);
    ci->disk_inode.i_mtime_sec = inode_get_mtime_sec(inode);
    ci->disk_inode.i_mtime_nsec = inode_get_mtime_nsec(inode);

    struct buffer_head *bh = sb_bread(sb, sbi->inode_begin + inode->i_ino);
    if (!bh) {
        return -EIO;
    }
    memcpy(bh->b_data, ci, PAGE_SIZE);
    mark_buffer_dirty(bh);
    brelse(bh);
    return 0;
}

static void bbfs_put_super(struct super_block *sb) {
    struct bbfs_sb_info *sbi = sb->s_fs_info;
    if (sbi) {
        kfree(sbi);
    }
}

static int bbfs_sync_fs(struct super_block *sb, int wait) {
    struct bbfs_sb_info *sbi = sb->s_fs_info;
    struct buffer_head *bh = sb_bread(sb, 0);
    if (!bh) {
        return -EIO;
    }
    memcpy(bh->b_data, sbi, PAGE_SIZE);
    mark_buffer_dirty(bh);
    if (wait) {
        sync_dirty_buffer(bh);
    }
    brelse(bh);
    return 0;
}

static struct super_operations bbfs_sops = {
    .put_super = bbfs_put_super,
    .alloc_inode = bbfs_alloc_inode,
    .destroy_inode = bbfs_destroy_inode,
    .write_inode = bbfs_write_inode,
    .sync_fs = bbfs_sync_fs,
};

int bbfs_fill_super(struct super_block *sb, void *data, int silent) {
    sb->s_magic = BBFS_MAGIC;
    sb_set_blocksize(sb, PAGE_SIZE);
    sb->s_time_gran = 1;
    sb->s_maxbytes = MAX_LFS_FILESIZE;
    sb->s_op = &bbfs_sops;

    struct buffer_head *bh = sb_bread(sb, 0);
    if (!bh) {
        return -EIO;
    }
    struct bbfs_sb_info *sbi = kzalloc(sizeof(struct bbfs_sb_info), GFP_KERNEL);
    if (!sbi) {
        kfree(bh);
        return -ENOMEM;
    }
    sb->s_fs_info = sbi;
    memcpy(&sbi->disk_sb, bh->b_data, sizeof(struct bbfs_sb));
    sbi->sb_begin = 0;
    sbi->sb_end = sbi->imap_begin = sbi->sb_begin + sbi->disk_sb.nr_sb;
    sbi->imap_end = sbi->bmap_begin = sbi->imap_begin + sbi->disk_sb.nr_imap;
    sbi->bmap_end = sbi->inode_begin = sbi->bmap_begin + sbi->disk_sb.nr_bmap;
    sbi->inode_end = sbi->block_begin = sbi->inode_begin + sbi->disk_sb.nr_inodes;
    sbi->block_end = sbi->block_begin + sbi->disk_sb.nr_blocks;
    brelse(bh);

    if (sbi->disk_sb.magic != sb->s_magic) {
        kfree(sbi);
        return -EINVAL;
    }

    struct inode *root_inode = bbfs_iget(sb, 0);
    if (IS_ERR(root_inode)) {
        kfree(sbi);
        return PTR_ERR(root_inode);
    }

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        iput(root_inode);
        kfree(sbi);
        return -ENOMEM;
    }

    pr_info("sb    [%6lld, %6lld)\n", sbi->sb_begin, sbi->sb_end);
    pr_info("imap  [%6lld, %6lld)\n", sbi->imap_begin, sbi->imap_end);
    pr_info("bmap  [%6lld, %6lld)\n", sbi->bmap_begin, sbi->bmap_end);
    pr_info("inode [%6lld, %6lld)\n", sbi->inode_begin, sbi->inode_end);
    pr_info("block [%6lld, %6lld)\n", sbi->block_begin, sbi->block_end);

    return 0;
}
