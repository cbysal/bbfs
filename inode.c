#ifndef __LINUX_KERNEL__
#define __LINUX_KERNEL__
#endif

#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stat.h>

#include "fs.h"

static const struct inode_operations bbfs_inode_ops;
static const struct inode_operations bbfs_symlink_inode_ops;

static unsigned long bbfs_find_and_mark_free_inode(struct super_block *sb) {
    struct bbfs_sb_info *sbi = BBFS_SB(sb);
    for (unsigned long i = 0; i < sbi->disk_sb.nr_imap; i++) {
        struct buffer_head *bh = sb_bread(sb, sbi->imap_begin + i);
        struct bbfs_imap_block *imap_blk = (struct bbfs_imap_block *)bh->b_data;
        for (unsigned long j = 0; j < sizeof(struct bbfs_imap_block) / sizeof(uint32_t); j++) {
            if (!imap_blk->blocks[j]) {
                imap_blk->blocks[j] = 1;
                mark_buffer_dirty(bh);
                brelse(bh);
                return i * (sizeof(struct bbfs_imap_block) / sizeof(uint32_t)) + j;
            }
        }
        brelse(bh);
    }
    return LONG_MAX;
}

unsigned long bbfs_find_and_mark_free_block(struct super_block *sb, int level) {
    struct bbfs_sb_info *sbi = BBFS_SB(sb);
    unsigned long blk_start = 0;
    unsigned long blk_num = 1ul << level;
    for (blk_start = 0; blk_start < sbi->disk_sb.nr_blocks; blk_start += blk_num) {
        bool found = true;
        for (int i = 0; i < blk_num; i++) {
            struct buffer_head *bh =
                sb_bread(sb, sbi->bmap_begin + (blk_start + i) / (sizeof(struct bbfs_sb) / sizeof(uint32_t)));
            struct bbfs_bmap_block *bmap_blk = (struct bbfs_bmap_block *)bh->b_data;
            if (bmap_blk->blocks[(blk_start + i) % (sizeof(struct bbfs_sb) / sizeof(uint32_t))]) {
                found = false;
                brelse(bh);
                break;
            }
            brelse(bh);
        }
        if (found) {
            for (int i = 0; i < blk_num; i++) {
                struct buffer_head *bh =
                    sb_bread(sb, sbi->bmap_begin + (blk_start + i) / (sizeof(struct bbfs_sb) / sizeof(uint32_t)));
                struct bbfs_bmap_block *bmap_blk = (struct bbfs_bmap_block *)bh->b_data;
                bmap_blk->blocks[(blk_start + i) % (sizeof(struct bbfs_sb) / sizeof(uint32_t))] = 1;
                mark_buffer_dirty(bh);
                brelse(bh);
            }
            return blk_start;
        }
    }
    return LONG_MAX;
}

struct inode *bbfs_iget(struct super_block *sb, unsigned long ino) {
    struct bbfs_sb_info *sbi = BBFS_SB(sb);

    if (ino >= sbi->disk_sb.nr_inodes) {
        return ERR_PTR(-EINVAL);
    }

    struct inode *inode = iget_locked(sb, ino);
    if (!inode) {
        return ERR_PTR(-ENOMEM);
    }

    if (!(inode->i_state & I_NEW)) {
        return inode;
    }

    struct bbfs_inode_info *ci = BBFS_INODE(inode);
    struct buffer_head *bh = sb_bread(sb, sbi->inode_begin + ino);
    if (!bh) {
        brelse(bh);
        iget_failed(inode);
        return ERR_PTR(-EIO);
    }
    memcpy(ci, bh->b_data, PAGE_SIZE);
    brelse(bh);

    inode->i_ino = ino;
    inode->i_sb = sb;

    inode->i_mode = ci->disk_inode.i_mode;
    i_uid_write(inode, ci->disk_inode.i_uid);
    i_gid_write(inode, ci->disk_inode.i_gid);
    inode->i_size = ci->disk_inode.i_size;

    inode_set_ctime(inode, ci->disk_inode.i_ctime_sec, ci->disk_inode.i_ctime_nsec);
    inode_set_atime(inode, ci->disk_inode.i_atime_sec, ci->disk_inode.i_atime_nsec);
    inode_set_mtime(inode, ci->disk_inode.i_mtime_sec, ci->disk_inode.i_mtime_nsec);

    set_nlink(inode, ci->disk_inode.i_nlink);

    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &bbfs_inode_ops;
        inode->i_fop = &bbfs_dir_ops;
    } else if (S_ISREG(inode->i_mode)) {
        inode->i_op = &bbfs_inode_ops;
        inode->i_fop = &bbfs_file_ops;
        inode->i_mapping->a_ops = &bbfs_aops;
    } else if (S_ISLNK(inode->i_mode)) {
        inode->i_op = &bbfs_symlink_inode_ops;
    }

    unlock_new_inode(inode);
    return inode;
}

static struct inode *bbfs_new_inode(struct inode *dir, mode_t mode) {
    struct super_block *sb = dir->i_sb;

    unsigned long ino = bbfs_find_and_mark_free_inode(sb);
    struct inode *inode = bbfs_iget(sb, ino);
    if (IS_ERR(inode)) {
        return inode;
    }

    struct bbfs_inode_info *ci = BBFS_INODE(inode);
    memset(&ci->disk_inode, 0, sizeof(struct bbfs_inode));
    ci->disk_inode.valid = 1;
    inode->i_mode = mode;
    inode->i_uid = current_uid();
    inode->i_gid = current_gid();
    struct timespec64 cur_time = current_time(inode);
    inode_set_ctime_current(inode);
    inode_set_atime_to_ts(inode, cur_time);
    inode_set_ctime_to_ts(inode, cur_time);
    inode_set_mtime_to_ts(inode, cur_time);

    if (S_ISDIR(inode->i_mode)) {
        inode->i_size = sizeof(struct bbfs_inode);
        inode->i_op = &bbfs_inode_ops;
        inode->i_fop = &bbfs_dir_ops;
        set_nlink(inode, 2);
    } else if (S_ISREG(inode->i_mode)) {
        inode->i_size = 0;
        inode->i_op = &bbfs_inode_ops;
        inode->i_fop = &bbfs_file_ops;
        inode->i_mapping->a_ops = &bbfs_aops;
        set_nlink(inode, 1);
    } else if (S_ISLNK(inode->i_mode)) {
        inode->i_size = 0;
        inode->i_op = &bbfs_symlink_inode_ops;
        set_nlink(inode, 1);
    }

    inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
    return inode;
}

static struct dentry *bbfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags) {
    struct super_block *sb = dir->i_sb;
    struct bbfs_sb_info *sbi = sb->s_fs_info;
    struct bbfs_inode_info *ci = BBFS_INODE(dir);

    for (int i = 0; i < ci->disk_inode.l_num; i++) {
        unsigned long blk_start = ci->disk_inode.levels[i];
        unsigned long blk_num = 1ul << i;
        for (int j = blk_start; j < blk_start + blk_num; j++) {
            struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
            for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
                struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
                if (!ent->valid) {
                    continue;
                }
                if (!strcmp(ent->name, dentry->d_name.name)) {
                    struct inode *inode = bbfs_iget(sb, ent->ino);
                    d_add(dentry, inode);
                    brelse(bh);
                    return NULL;
                }
            }
            brelse(bh);
        }
    }
    return NULL;
}

static int bbfs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    struct super_block *sb = dir->i_sb;
    struct bbfs_sb_info *sbi = sb->s_fs_info;
    struct bbfs_inode_info *dir_ci = BBFS_INODE(dir);

    mode |= S_IFREG;
    for (int i = 0; i < dir_ci->disk_inode.l_num; i++) {
        unsigned long blk_start = dir_ci->disk_inode.levels[i];
        unsigned long blk_num = 1ul << i;
        for (int j = blk_start; j < blk_start + blk_num; j++) {
            struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
            for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
                struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
                if (!ent->valid) {
                    struct inode *file = bbfs_new_inode(dir, mode);
                    mark_inode_dirty(file);
                    ent->valid = 1;
                    ent->type = DT_REG;
                    ent->ino = file->i_ino;
                    strcpy(ent->name, dentry->d_name.name);
                    mark_buffer_dirty(bh);
                    brelse(bh);
                    d_instantiate(dentry, file);
                    return 0;
                }
            }
            brelse(bh);
        }
    }

    int level = dir_ci->disk_inode.l_num++;
    unsigned long blk_start = bbfs_find_and_mark_free_block(sb, level);
    dir_ci->disk_inode.levels[level] = blk_start;
    mark_inode_dirty(dir);

    unsigned long blk_num = 1ul << level;
    for (int j = blk_start; j < blk_start + blk_num; j++) {
        struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
        for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
            struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
            if (!ent->valid) {
                struct inode *file = bbfs_new_inode(dir, mode);
                mark_inode_dirty(file);
                ent->valid = 1;
                ent->type = DT_REG;
                ent->ino = file->i_ino;
                strcpy(ent->name, dentry->d_name.name);
                mark_buffer_dirty(bh);
                brelse(bh);
                d_instantiate(dentry, file);
                return 0;
            }
        }
        brelse(bh);
    }
    return -1;
}

static int bbfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry) {
    struct super_block *sb = dir->i_sb;
    struct bbfs_sb_info *sbi = BBFS_SB(sb);
    struct bbfs_inode_info *dir_ci = BBFS_INODE(dir);
    struct inode *file = d_inode(old_dentry);

    for (int i = 0; i < dir_ci->disk_inode.l_num; i++) {
        unsigned long blk_start = dir_ci->disk_inode.levels[i];
        unsigned long blk_num = 1ul << i;
        for (int j = blk_start; j < blk_start + blk_num; j++) {
            struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
            for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
                struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
                if (!ent->valid) {
                    ent->valid = 1;
                    if (S_ISDIR(file->i_mode)) {
                        ent->type = DT_DIR;
                    } else if (S_ISREG(file->i_mode)) {
                        ent->type = DT_REG;
                    } else if (S_ISLNK(file->i_mode)) {
                        ent->type = DT_LNK;
                    }
                    ent->ino = file->i_ino;
                    strcpy(ent->name, dentry->d_name.name);
                    mark_buffer_dirty(bh);
                    brelse(bh);
                    inode_inc_link_count(file);
                    ihold(file);
                    d_instantiate(dentry, file);
                    return 0;
                }
            }
            brelse(bh);
        }
    }

    return -1;
}

static int bbfs_unlink(struct inode *dir, struct dentry *dentry) {
    struct super_block *sb = dir->i_sb;
    struct bbfs_sb_info *sbi = sb->s_fs_info;
    struct bbfs_inode_info *dir_ci = BBFS_INODE(dir);
    struct inode *file = dentry->d_inode;
    struct bbfs_inode_info *file_ci = BBFS_INODE(file);
    bool found = false;

    for (int i = 0; i < dir_ci->disk_inode.l_num; i++) {
        unsigned long blk_start = dir_ci->disk_inode.levels[i];
        unsigned long blk_num = 1ul << i;
        for (int j = blk_start; j < blk_start + blk_num; j++) {
            struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
            for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
                struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
                if (ent->valid && !strcmp(ent->name, dentry->d_name.name)) {
                    found = true;
                    memset(ent, 0, sizeof(struct bbfs_entry));
                    mark_buffer_dirty(bh);
                    inode_dec_link_count(file);
                    break;
                }
            }
            brelse(bh);
            if (found) {
                break;
            }
        }
        if (found) {
            break;
        }
    }

    if (file->i_nlink) {
        return 0;
    }

    for (int i = 0; i < file_ci->disk_inode.l_num; i++) {
        unsigned long blk_start = dir_ci->disk_inode.levels[i];
        unsigned long blk_num = 1ul << i;
        for (int j = blk_start; j < blk_start + blk_num; j++) {
            struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
            struct bbfs_bmap_block *bmap_blk = (struct bbfs_bmap_block *)bh->b_data;
            memset(bmap_blk, 0, sizeof(struct bbfs_bmap_block));
            mark_buffer_dirty(bh);
            brelse(bh);
        }
    }

    struct buffer_head *bh =
        sb_bread(sb, sbi->imap_begin + file->i_ino / (sizeof(struct bbfs_imap_block) / sizeof(uint32_t)));
    struct bbfs_imap_block *imap_blk = (struct bbfs_imap_block *)bh->b_data;
    imap_blk->blocks[file->i_ino % (sizeof(struct bbfs_imap_block) / sizeof(uint32_t))] = 0;
    mark_buffer_dirty(bh);
    brelse(bh);

    return 0;
}

static int bbfs_rename(struct mnt_idmap *idmap, struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir,
                       struct dentry *new_dentry, unsigned int flags) {
    struct bbfs_inode_info *old_dir_ci = BBFS_INODE(old_dir);
    struct bbfs_inode_info *new_dir_ci = BBFS_INODE(new_dir);
    struct inode *file = old_dentry->d_inode;
    struct super_block *sb = file->i_sb;
    struct bbfs_sb_info *sbi = BBFS_SB(sb);
    bool found = false;
    struct bbfs_entry entry;

    for (int i = 0; i < old_dir_ci->disk_inode.l_num; i++) {
        unsigned long blk_start = old_dir_ci->disk_inode.levels[i];
        unsigned long blk_num = 1ul << i;
        for (int j = blk_start; j < blk_start + blk_num; j++) {
            struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
            for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
                struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
                if (ent->valid && ent->ino == file->i_ino) {
                    found = true;
                    memcpy(&entry, ent, sizeof(struct bbfs_entry));
                    memset(ent, 0, sizeof(struct bbfs_entry));
                    mark_buffer_dirty(bh);
                    if (S_ISDIR(file->i_mode)) {
                        inode_dec_link_count(old_dir);
                    }
                    break;
                }
            }
            brelse(bh);
            if (found) {
                break;
            }
        }
        if (found) {
            break;
        }
    }

    strcpy(entry.name, new_dentry->d_name.name);

    for (int i = 0; i < new_dir_ci->disk_inode.l_num; i++) {
        unsigned long blk_start = new_dir_ci->disk_inode.levels[i];
        unsigned long blk_num = 1ul << i;
        for (int j = blk_start; j < blk_start + blk_num; j++) {
            struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
            for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
                struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
                if (!ent->valid) {
                    memcpy(ent, &entry, sizeof(struct bbfs_entry));
                    mark_buffer_dirty(bh);
                    brelse(bh);
                    if (S_ISDIR(file->i_mode)) {
                        inode_inc_link_count(new_dir);
                    }
                    return 0;
                }
            }
            brelse(bh);
        }
    }

    int level = new_dir_ci->disk_inode.l_num++;
    unsigned long blk_start = bbfs_find_and_mark_free_block(sb, level);
    new_dir_ci->disk_inode.levels[level] = blk_start;
    mark_inode_dirty(new_dir);

    unsigned long blk_num = 1ul << level;
    for (int j = blk_start; j < blk_start + blk_num; j++) {
        struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
        for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
            struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
            if (!ent->valid) {
                memcpy(ent, &entry, sizeof(struct bbfs_entry));
                mark_buffer_dirty(bh);
                brelse(bh);
                if (S_ISDIR(file->i_mode)) {
                    inode_inc_link_count(new_dir);
                }
                return 0;
            }
        }
        brelse(bh);
    }
    return -1;
}

static int bbfs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode) {
    struct super_block *sb = dir->i_sb;
    struct bbfs_sb_info *sbi = sb->s_fs_info;
    struct bbfs_inode_info *dir_ci = BBFS_INODE(dir);

    mode |= S_IFDIR;
    for (int i = 0; i < dir_ci->disk_inode.l_num; i++) {
        unsigned long blk_start = dir_ci->disk_inode.levels[i];
        unsigned long blk_num = 1ul << i;
        for (int j = blk_start; j < blk_start + blk_num; j++) {
            struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
            for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
                struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
                if (!ent->valid) {
                    struct inode *file = bbfs_new_inode(dir, mode);
                    mark_inode_dirty(file);
                    d_instantiate(dentry, file);
                    ent->valid = 1;
                    ent->type = DT_DIR;
                    ent->ino = file->i_ino;
                    strcpy(ent->name, dentry->d_name.name);
                    mark_buffer_dirty(bh);
                    brelse(bh);
                    inode_inc_link_count(dir);
                    return 0;
                }
            }
            brelse(bh);
        }
    }

    int level = dir_ci->disk_inode.l_num++;
    unsigned long blk_start = bbfs_find_and_mark_free_block(sb, level);
    dir_ci->disk_inode.levels[level] = blk_start;
    mark_inode_dirty(dir);

    unsigned long blk_num = 1ul << level;
    for (int j = blk_start; j < blk_start + blk_num; j++) {
        struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
        for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
            struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
            if (!ent->valid) {
                struct inode *file = bbfs_new_inode(dir, mode);
                ent->ino = file->i_ino;
                mark_inode_dirty(file);
                d_instantiate(dentry, file);
                ent->valid = 1;
                ent->type = DT_DIR;
                strcpy(ent->name, dentry->d_name.name);
                mark_buffer_dirty(bh);
                brelse(bh);
                inode_inc_link_count(dir);
                return 0;
            }
        }
        brelse(bh);
    }
    return -1;
}

static int bbfs_rmdir(struct inode *dir, struct dentry *dentry) {
    struct super_block *sb = dir->i_sb;
    struct bbfs_sb_info *sbi = sb->s_fs_info;
    struct bbfs_inode_info *ci = BBFS_INODE(dir);
    struct inode *file = dentry->d_inode;
    bool found = false;

    for (int i = 0; i < ci->disk_inode.l_num; i++) {
        unsigned long blk_start = ci->disk_inode.levels[i];
        unsigned long blk_num = 1ul << i;
        for (int j = blk_start; j < blk_start + blk_num; j++) {
            struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
            for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
                struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
                if (ent->valid && !strcmp(ent->name, dentry->d_name.name)) {
                    found = true;
                    memset(ent, 0, sizeof(struct bbfs_entry));
                    mark_buffer_dirty(bh);
                    inode_dec_link_count(file);
                    break;
                }
            }
            brelse(bh);
            if (found) {
                break;
            }
        }
        if (found) {
            break;
        }
    }

    if (file->i_nlink < 2) {
        return 0;
    }

    struct buffer_head *bh =
        sb_bread(sb, sbi->imap_begin + file->i_ino / (sizeof(struct bbfs_imap_block) / sizeof(uint32_t)));
    struct bbfs_imap_block *imap_blk = (struct bbfs_imap_block *)bh->b_data;
    imap_blk->blocks[file->i_ino % (sizeof(struct bbfs_imap_block) / sizeof(uint32_t))] = 0;
    mark_buffer_dirty(bh);
    brelse(bh);
    inode_dec_link_count(dir);
    iput(file);

    return 0;
}

static int bbfs_symlink(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, const char *symname) {
    struct super_block *sb = dir->i_sb;
    struct bbfs_sb_info *sbi = sb->s_fs_info;
    struct bbfs_inode_info *dir_ci = BBFS_INODE(dir);
    struct bbfs_inode_info *file_ci;

    for (int i = 0; i < dir_ci->disk_inode.l_num; i++) {
        unsigned long blk_start = dir_ci->disk_inode.levels[i];
        unsigned long blk_num = 1ul << i;
        for (int j = blk_start; j < blk_start + blk_num; j++) {
            struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
            for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
                struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
                if (!ent->valid) {
                    struct inode *file = bbfs_new_inode(dir, S_IFLNK | S_IRWXUGO);
                    file_ci = BBFS_INODE(file);
                    file->i_link = file_ci->disk_inode.i_link;
                    file_ci->disk_inode.i_size = strlen(symname);
                    strcpy(file_ci->disk_inode.i_link, symname);
                    mark_inode_dirty(file);
                    ent->valid = 1;
                    ent->type = DT_LNK;
                    ent->ino = file->i_ino;
                    strcpy(ent->name, dentry->d_name.name);
                    mark_buffer_dirty(bh);
                    brelse(bh);
                    d_instantiate(dentry, file);
                    return 0;
                }
            }
            brelse(bh);
        }
    }

    int level = dir_ci->disk_inode.l_num++;
    unsigned long blk_start = bbfs_find_and_mark_free_block(sb, level);
    dir_ci->disk_inode.levels[level] = blk_start;
    mark_inode_dirty(dir);

    unsigned long blk_num = 1ul << level;
    for (int j = blk_start; j < blk_start + blk_num; j++) {
        struct buffer_head *bh = sb_bread(sb, sbi->block_begin + j);
        for (int k = 0; k < PAGE_SIZE; k += sizeof(struct bbfs_entry)) {
            struct bbfs_entry *ent = (struct bbfs_entry *)(bh->b_data + k);
            if (!ent->valid) {
                struct inode *file = bbfs_new_inode(dir, S_IFLNK | S_IRWXUGO);
                file_ci = BBFS_INODE(file);
                file->i_link = file_ci->disk_inode.i_link;
                file_ci->disk_inode.i_size = strlen(symname);
                strcpy(file_ci->disk_inode.i_link, symname);
                mark_inode_dirty(file);
                ent->valid = 1;
                ent->type = DT_LNK;
                ent->ino = file->i_ino;
                strcpy(ent->name, dentry->d_name.name);
                mark_buffer_dirty(bh);
                brelse(bh);
                d_instantiate(dentry, file);
                return 0;
            }
        }
        brelse(bh);
    }
    return -1;
}

static const struct inode_operations bbfs_inode_ops = {
    .lookup = bbfs_lookup,
    .create = bbfs_create,
    .link = bbfs_link,
    .unlink = bbfs_unlink,
    .mkdir = bbfs_mkdir,
    .rmdir = bbfs_rmdir,
    .rename = bbfs_rename,
    .symlink = bbfs_symlink,
};

static const char *bbfs_get_link(struct dentry *dentry, struct inode *inode, struct delayed_call *callback) {
    return inode->i_link;
}

static const struct inode_operations bbfs_symlink_inode_ops = {
    .get_link = bbfs_get_link,
};
