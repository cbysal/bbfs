#ifndef __LINUX_KERNEL__
#define __LINUX_KERNEL__
#endif

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "fs.h"

static int bbfs_iterate(struct file *dir, struct dir_context *ctx) {
    struct inode *inode = file_inode(dir);
    struct bbfs_inode_info *ci = BBFS_INODE(inode);
    struct super_block *sb = inode->i_sb;
    struct bbfs_sb_info *sbi = sb->s_fs_info;

    if (!S_ISDIR(inode->i_mode))
        return -ENOTDIR;

    if (!dir_emit_dots(dir, ctx))
        return 0;

    int pos = ctx->pos - 2;
    for (int i = 0; i < ci->disk_inode.l_num; i++) {
        uint32_t blk_start = ci->disk_inode.levels[i];
        uint32_t blk_num = 1 << i;
        for (int j = blk_start; j < blk_start + blk_num; j++) {
            struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
            for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
                struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
                if (ent->valid) {
                    if (pos > 0) {
                        pos--;
                        continue;
                    }
                    if (!dir_emit(ctx, ent->name, NAME_MAX, ent->ino, ent->type))
                        break;
                    ctx->pos++;
                }
            }
            brelse(bh);
        }
    }
    return 0;
}

const struct file_operations bbfs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = bbfs_iterate,
};
