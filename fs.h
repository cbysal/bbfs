#ifndef _FS_H
#define _FS_H

#define BBFS_MAGIC 0x53464242
#define MAX_BBFS_FILESIZE MAX_LFS_FILESIZE
#define MAX_LEVEL 1005
#define MAX_SYMLINK_LEN 4024

struct bbfs_sb {
    uint32_t magic;
    uint32_t nr_sb;
    uint32_t nr_imap;
    uint32_t nr_bmap;
    uint32_t nr_inodes;
    uint32_t nr_blocks;
    char padding[4072];
};

struct bbfs_inode {
    uint32_t valid;
    uint32_t i_mode;
    uint32_t i_uid;
    uint32_t i_gid;
    uint32_t i_size;
    uint32_t i_nlink;
    uint64_t i_ctime_sec;
    uint64_t i_ctime_nsec;
    uint64_t i_atime_sec;
    uint64_t i_atime_nsec;
    uint64_t i_mtime_sec;
    uint64_t i_mtime_nsec;
    union {
        struct {
            uint32_t l_num;
            uint32_t levels[MAX_LEVEL];
        };
        char i_link[MAX_SYMLINK_LEN];
    };
};

struct bbfs_imap_block {
    uint32_t blocks[1024];
};

struct bbfs_bmap_block {
    uint32_t blocks[1024];
};

struct bbfs_entry {
    uint32_t valid;
    uint32_t type;
    uint32_t ino;
    char name[NAME_MAX + 1];
    char padding[244];
};

#ifdef __LINUX_KERNEL__
struct bbfs_sb_info {
    struct bbfs_sb disk_sb;
    uint64_t sb_begin, sb_end;
    uint64_t imap_begin, imap_end;
    uint64_t bmap_begin, bmap_end;
    uint64_t inode_begin, inode_end;
    uint64_t block_begin, block_end;
    char *i_map;
    char *d_map;
};

struct bbfs_inode_info {
    struct bbfs_inode disk_inode;
    struct inode vfs_inode;
};

int bbfs_fill_super(struct super_block *sb, void *data, int silent);

int bbfs_init_inode_cache(void);
void bbfs_destroy_inode_cache(void);
struct inode *bbfs_iget(struct super_block *sb, unsigned long ino);
unsigned long bbfs_find_and_mark_free_block(struct super_block *sb, int level);

extern const struct file_operations bbfs_file_ops;
extern const struct file_operations bbfs_dir_ops;
extern const struct address_space_operations bbfs_aops;

#define BBFS_SB(sb) (sb->s_fs_info)
#define BBFS_INODE(inode) (container_of(inode, struct bbfs_inode_info, vfs_inode))
#endif

#endif
