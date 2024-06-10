#include <fcntl.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "fs.h"

int main(int argc, char **argv) {
    if (argc != 2) {
        return -1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        return -1;
    }

    struct stat stat_buf;
    if (fstat(fd, &stat_buf)) {
        close(fd);
        return -1;
    }

    if ((stat_buf.st_mode & S_IFMT) == S_IFBLK) {
        if (ioctl(fd, BLKGETSIZE64, &stat_buf.st_size)) {
            close(fd);
            return -1;
        }
    }

    int page_size = getpagesize();
    unsigned long nr_imap = (stat_buf.st_size - sizeof(struct bbfs_sb)) / (page_size + sizeof(uint32_t)) / 17 /
                            (page_size / sizeof(uint32_t));
    unsigned long nr_bmap = nr_imap * 15;
    unsigned long nr_inodes = nr_imap * (page_size / sizeof(uint32_t));
    unsigned long nr_blocks = nr_bmap * (page_size / sizeof(uint32_t));

    struct bbfs_sb sb = {
        .magic = BBFS_MAGIC,
        .nr_sb = sizeof(struct bbfs_sb) / page_size,
        .nr_imap = nr_imap,
        .nr_bmap = nr_bmap,
        .nr_inodes = nr_inodes,
        .nr_blocks = nr_blocks,
    };
    if (write(fd, &sb, page_size) < 0) {
        close(fd);
        return -1;
    }

    struct bbfs_imap_block imap_blk = {};
    imap_blk.blocks[0] = 1;
    if (write(fd, &imap_blk, page_size) < 0) {
        close(fd);
        return -1;
    }

    imap_blk.blocks[0] = 0;
    for (unsigned long i = 1; i < nr_imap; i++) {
        if (write(fd, &imap_blk, page_size) < 0) {
            close(fd);
            return -1;
        }
    }

    struct bbfs_bmap_block bmap_blk = {};
    for (unsigned long i = 0; i < nr_bmap; i++) {
        if (write(fd, &bmap_blk, page_size) < 0) {
            close(fd);
            return -1;
        }
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct bbfs_inode root_inode = {
        .valid = 1,
        .i_mode = S_IFDIR | 0755,
        .i_uid = getuid(),
        .i_gid = getgid(),
        .i_size = sizeof(struct bbfs_inode),
        .i_ctime_sec = ts.tv_sec,
        .i_ctime_nsec = ts.tv_nsec,
        .i_atime_sec = ts.tv_sec,
        .i_atime_nsec = ts.tv_nsec,
        .i_mtime_sec = ts.tv_sec,
        .i_mtime_nsec = ts.tv_nsec,
        .i_nlink = 2,
    };
    if (write(fd, &root_inode, page_size) < 0) {
        close(fd);
        return -1;
    }

    struct bbfs_inode zero_inode = {};
    for (int i = 1; i < sb.nr_inodes; i++) {
        if (write(fd, &zero_inode, page_size) < 0) {
            close(fd);
            return -1;
        }
    }

    close(fd);
    return 0;
}
