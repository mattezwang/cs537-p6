#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include "wfs.h"

#include <libgen.h>

void *mapped_region;
struct wfs_sb *superblock;

// Helper functions
struct wfs_inode *find_inode(int inode_num) {
    if (inode_num < 0 || inode_num >= superblock->num_inodes) {
        return NULL;
    }
    return (struct wfs_inode *)((char *)mapped_region + superblock->i_blocks_ptr + inode_num * BLOCK_SIZE);
}

off_t block_offset(off_t block) {
    return superblock->d_blocks_ptr + block * BLOCK_SIZE;
}

int allocate_block() {
    char *data_bitmap = (char *)mapped_region + superblock->d_bitmap_ptr;
    for (int i = 0; i < superblock->num_data_blocks; i++) {
        if (!(data_bitmap[i / 8] & (1 << (i % 8)))) {
            data_bitmap[i / 8] |= (1 << (i % 8));
            return i;
        }
    }
    return -ENOSPC;
}

void free_block(int block) {
    char *data_bitmap = (char *)mapped_region + superblock->d_bitmap_ptr;
    data_bitmap[block / 8] &= ~(1 << (block % 8));
}

// Implementations
static int my_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    struct wfs_inode *inode = find_inode(0); // Start with root inode
    if (!inode) return -ENOENT;

    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_mode = inode->mode;
    stbuf->st_nlink = inode->nlinks;
    stbuf->st_size = inode->size;
    stbuf->st_atime = inode->atim;
    stbuf->st_mtime = inode->mtim;
    stbuf->st_ctime = inode->ctim;

    return 0;
}

static int my_mknod(const char *path, mode_t mode, dev_t rdev) {
    char *dir_path = strdup(path); // Duplicate path to avoid modifying the original
    if (!dir_path) {
        return -ENOMEM; // Handle memory allocation failure
    }

    char *file_name = basename(dir_path); // Extract the file name
    if (!file_name) {
        free(dir_path);
        return -EINVAL; // Invalid argument
    }

    struct wfs_inode *parent_inode = find_inode(0); // Assume root for simplicity
    if (!parent_inode || parent_inode->mode != (S_IFDIR | 0755)) {
        free(dir_path);
        return -ENOENT; // Parent directory not found
    }

    // Check if the file already exists
    struct wfs_dentry *entries = (struct wfs_dentry *)((char *)mapped_region + block_offset(parent_inode->blocks[0]));
    for (int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
        if (strcmp(entries[i].name, file_name) == 0) {
            free(dir_path);
            return -EEXIST; // File exists
        }
    }

    // Allocate a new inode and directory entry
    int new_inode_num = -1;
    char *inode_bitmap = (char *)mapped_region + superblock->i_bitmap_ptr;
    for (int i = 0; i < superblock->num_inodes; i++) {
        if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
            inode_bitmap[i / 8] |= (1 << (i % 8));
            new_inode_num = i;
            break;
        }
    }
    if (new_inode_num == -1) {
        free(dir_path);
        return -ENOSPC; // No space left
    }

    struct wfs_inode *new_inode = find_inode(new_inode_num);
    memset(new_inode, 0, sizeof(struct wfs_inode));
    new_inode->mode = mode;
    new_inode->uid = getuid();
    new_inode->gid = getgid();
    new_inode->nlinks = 1;
    new_inode->atim = time(NULL);
    new_inode->mtim = time(NULL);
    new_inode->ctim = time(NULL);

    strcpy(entries[0].name, file_name);
    entries[0].num = new_inode_num;

    free(dir_path);
    return 0;
}


static int my_mkdir(const char *path, mode_t mode) {
    char *dir_path = strdup(path);
    if (!dir_path) {
        return -ENOMEM;
    }

    char *dir_name = basename(dir_path);
    if (!dir_name) {
        free(dir_path);
        return -EINVAL;
    }

    // Find the parent directory's inode (root inode in this example)
    struct wfs_inode *parent_inode = find_inode(0);
    if (!parent_inode || parent_inode->mode != (S_IFDIR | 0755)) {
        free(dir_path);
        return -ENOENT;
    }

    // Check if the directory already exists
    struct wfs_dentry *entries = (struct wfs_dentry *)((char *)mapped_region + block_offset(parent_inode->blocks[0]));
    for (int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
        if (strcmp(entries[i].name, dir_name) == 0) {
            free(dir_path);
            return -EEXIST;
        }
    }

    // Allocate a new inode for the directory
    int new_inode_num = -1;
    char *inode_bitmap = (char *)mapped_region + superblock->i_bitmap_ptr;
    for (int i = 0; i < superblock->num_inodes; i++) {
        if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
            inode_bitmap[i / 8] |= (1 << (i % 8));
            new_inode_num = i;
            break;
        }
    }
    if (new_inode_num == -1) {
        free(dir_path);
        return -ENOSPC;
    }

    struct wfs_inode *new_inode = find_inode(new_inode_num);
    memset(new_inode, 0, sizeof(struct wfs_inode));
    new_inode->mode = S_IFDIR | mode;
    new_inode->uid = getuid();
    new_inode->gid = getgid();
    new_inode->nlinks = 2; // "." and parent directory link
    new_inode->atim = time(NULL);
    new_inode->mtim = time(NULL);
    new_inode->ctim = time(NULL);

    // Allocate a data block for the new directory
    int new_block = allocate_block();
    if (new_block < 0) {
        free(dir_path);
        return -ENOSPC;
    }
    new_inode->blocks[0] = new_block;

    // Initialize the new directory's data block with "." and ".."
    struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)mapped_region + block_offset(new_block));
    memset(dir_entries, 0, BLOCK_SIZE);
    strcpy(dir_entries[0].name, ".");
    dir_entries[0].num = new_inode_num;
    strcpy(dir_entries[1].name, "..");
    dir_entries[1].num = 0; // Root directory inode

    // Add the new directory entry to the parent directory
    for (int i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
        if (entries[i].name[0] == '\0') {
            strcpy(entries[i].name, dir_name);
            entries[i].num = new_inode_num;
            break;
        }
    }

    free(dir_path);
    return 0;
}

static int my_unlink(const char *path) {
    struct wfs_inode *inode = find_inode(0); // Find inode for the file
    if (!inode) return -ENOENT;

    // Free data blocks
    for (int i = 0; i < D_BLOCK; i++) {
        if (inode->blocks[i]) free_block(inode->blocks[i]);
    }

    char *inode_bitmap = (char *)mapped_region + superblock->i_bitmap_ptr;
    inode_bitmap[inode->num / 8] &= ~(1 << (inode->num % 8));

    return 0;
}

static int my_rmdir(const char *path) {
    return my_unlink(path);
}

static int my_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct wfs_inode *inode = find_inode(0); // Get inode for the file
    if (!inode) return -ENOENT;

    size_t bytes_read = 0;
    while (size > 0) {
        int block_idx = offset / BLOCK_SIZE;
        if (block_idx >= D_BLOCK) break;

        off_t block_offset_val = block_offset(inode->blocks[block_idx]);
        size_t to_read = BLOCK_SIZE - (offset % BLOCK_SIZE);
        if (to_read > size) to_read = size;

        memcpy(buf + bytes_read, (char *)mapped_region + block_offset_val + (offset % BLOCK_SIZE), to_read);

        size -= to_read;
        offset += to_read;
        bytes_read += to_read;
    }

    return bytes_read;
}

static int my_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct wfs_inode *inode = find_inode(0); // Get inode for the file
    if (!inode) return -ENOENT;

    size_t bytes_written = 0;
    while (size > 0) {
        int block_idx = offset / BLOCK_SIZE;
        if (block_idx >= D_BLOCK) break;

        if (!inode->blocks[block_idx]) {
            int new_block = allocate_block();
            if (new_block < 0) return -ENOSPC;
            inode->blocks[block_idx] = new_block;
        }

        off_t block_offset_val = block_offset(inode->blocks[block_idx]);
        size_t to_write = BLOCK_SIZE - (offset % BLOCK_SIZE);
        if (to_write > size) to_write = size;

        memcpy((char *)mapped_region + block_offset_val + (offset % BLOCK_SIZE), buf + bytes_written, to_write);

        size -= to_write;
        offset += to_write;
        bytes_written += to_write;
    }

    inode->size = offset > inode->size ? offset : inode->size;
    return bytes_written;
}

static int my_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    struct wfs_inode *inode = find_inode(0); // Root inode for simplicity
    if (!inode) return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    return 0;
}

static struct fuse_operations ops = {
    .getattr = my_getattr,
    .mknod   = my_mknod,
    .mkdir   = my_mkdir,
    .unlink  = my_unlink,
    .rmdir   = my_rmdir,
    .read    = my_read,
    .write   = my_write,
    .readdir = my_readdir,
};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <disk1> <disk2> ... <mount_point> [FUSE options]\n", argv[0]);
        return 1;
    }

    // Determine the number of disk image files
    int num_disks = 0;
    for (int i = 1; i < argc - 1; i++) {
        if (argv[i][0] == '-') break; // Stop at the first FUSE option
        num_disks++;
    }

    if (num_disks < 2) {
        fprintf(stderr, "Error: At least two disk images are required.\n");
        return 1;
    }

    struct stat tmp;
    int fd;

    for (int i = 1; i <= num_disks; i++) {
        if (access(argv[i], F_OK) != 0) {
            fprintf(stderr, "Error: Disk image '%s' does not exist.\n", argv[i]);
            return 1;
        }

        fd = open(argv[i], O_RDWR);
        if (fd < 0) {
            perror("Error opening disk image");
            return 1;
        }

        if (fstat(fd, &tmp) < 0) {
            perror("Error getting file stats");
            close(fd);
            return 1;
        }

        mapped_region = mmap(NULL, tmp.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapped_region == MAP_FAILED) {
            perror("Error mapping memory");
            close(fd);
            return 1;
        }
        close(fd);
    }

    superblock = (struct wfs_sb *)mapped_region;

    // Pass FUSE options and mount point to fuse_main
    return fuse_main(argc - num_disks, &argv[num_disks + 1], &ops, NULL);
}

