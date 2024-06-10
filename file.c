#ifndef __LINUX_KERNEL__
#define __LINUX_KERNEL__
#endif

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mpage.h>

#include "fs.h"

static int bbfs_file_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create) {
    struct super_block *sb = inode->i_sb;
    struct bbfs_sb_info *sbi = BBFS_SB(sb);
    struct bbfs_inode_info *ci = BBFS_INODE(inode);

    int level = 0;
    int offset = iblock;
    while (offset >= (1 << level)) {
        offset -= 1 << level;
        level++;
    }

    if (!create && ci->disk_inode.l_num < level) {
        return 0;
    }

    while (ci->disk_inode.l_num <= level) {
        unsigned long blk_start = bbfs_find_and_mark_free_block(sb, ci->disk_inode.l_num);
        ci->disk_inode.levels[ci->disk_inode.l_num] = blk_start;
        ci->disk_inode.l_num++;
    }

    map_bh(bh_result, sb, sbi->block_begin + ci->disk_inode.levels[level] + offset);
    return 0;
}

static void bbfs_readahead(struct readahead_control *rac) { mpage_readahead(rac, bbfs_file_get_block); }

static int bbfs_writepages(struct address_space *mapping, struct writeback_control *wbc) {
    return mpage_writepages(mapping, wbc, bbfs_file_get_block);
}

static int bbfs_write_begin(struct file *file, struct address_space *mapping, loff_t pos, unsigned int len,
                            struct page **pagep, void **fsdata) {
    return block_write_begin(mapping, pos, len, pagep, bbfs_file_get_block);
}

static int bbfs_write_end(struct file *file, struct address_space *mapping, loff_t pos, unsigned int len,
                          unsigned int copied, struct page *page, void *fsdata) {
    struct inode *inode = file_inode(file);
    int ret;

    ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);

    struct timespec64 cur_time = current_time(inode);
    inode_set_mtime_to_ts(inode, cur_time);
    inode_set_ctime_to_ts(inode, cur_time);
    mark_inode_dirty(inode);

    return ret;
}

const struct address_space_operations bbfs_aops = {
    .writepages = bbfs_writepages,
    .readahead = bbfs_readahead,
    .write_begin = bbfs_write_begin,
    .write_end = bbfs_write_end,
};

const struct file_operations bbfs_file_ops = {
    .llseek = generic_file_llseek,
    .owner = THIS_MODULE,
    .read_iter = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
    .fsync = generic_file_fsync,
};
