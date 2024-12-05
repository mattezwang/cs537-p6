
#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <libgen.h>
#include "wfs.h"

void *mapped_region;
int return_code;

char* mmap_ptr(off_t offset) { 
    return (char *)mapped_region + offset;
}

size_t alloc_block(uint32_t *bitmap, size_t size) {
    for (uint32_t i = 0; i < size; i++) {
        uint32_t bitmap_region = bitmap[i];
        uint32_t k = 0;
        while (bitmap_region != UINT32_MAX && k < 32) {
            if (!((bitmap_region >> k) & 0x1)) {
                bitmap[i] = bitmap[i] | (0x1 << k);
                return i * 32 + k;
            }
            k++;
        }
    }
    return -1;
}

void free_bitmap(uint32_t pos, uint32_t *bitmap) {
    bitmap[pos / 32] -= 1 << (pos % 32);
}

off_t alloc_database() {
    struct wfs_sb *tmp = (struct wfs_sb *)mapped_region;
    off_t num = alloc_block((uint32_t *)mmap_ptr(tmp->d_bitmap_ptr), tmp->num_data_blocks / 32);
    if (num < 0) {
        return_code = -ENOSPC;  
        return -1;
    }
    return tmp->d_blocks_ptr + BLOCK_SIZE * num;
}

struct wfs_inode *alloc_inode() {
    struct wfs_sb *temp = (struct wfs_sb *)mapped_region;
    off_t num = alloc_block((uint32_t *)mmap_ptr(temp->i_bitmap_ptr), temp->num_inodes / 32);
    if (num < 0) {
        return_code = -ENOSPC;
        return NULL;
    }
    struct wfs_inode *inode = (struct wfs_inode *)((char *)mmap_ptr(temp->i_blocks_ptr) + num * BLOCK_SIZE);
    inode->num = num;
    return inode;
}

void free_inode(struct wfs_inode *inode) {
    // Clear the contents of the inode structure
    memset(inode, 0, sizeof(struct wfs_inode));

    // Calculate the block number of the inode based on its location in memory
    char *inode_start = (char *)inode;
    char *blocks_ptr = (char *)mmap_ptr(((struct wfs_sb *)mapped_region)->i_blocks_ptr);
    off_t block_index = (inode_start - blocks_ptr) / BLOCK_SIZE;

    // Free the corresponding block in the bitmap
    uint32_t *bitmap_ptr = (uint32_t *)mmap_ptr(((struct wfs_sb *)mapped_region)->i_bitmap_ptr);
    free_bitmap(block_index, bitmap_ptr);
}



struct wfs_inode *find_inode(int num) {
    // Get the inode bitmap pointer
    struct wfs_sb *sb = (struct wfs_sb *)mapped_region;
    uint32_t *bitmap = (uint32_t *)mmap_ptr(sb->i_bitmap_ptr);

    // Check if the inode number is valid in the bitmap
    int bitmap_index = num / 32;  // Index of the 32-bit integer in the bitmap
    int bit_pos = num % 32; // Bit position within the 32-bit integer
    int valid_inode = bitmap[bitmap_index] & (0x1 << bit_pos);

    // If valid, calculate the inode's memory address and return it
    if (valid_inode) {
        char *base = (char *)mmap_ptr(sb->i_blocks_ptr);
        return (struct wfs_inode *)(base + num * BLOCK_SIZE);
    }

    // Return NULL if the inode is invalid
    return NULL;
}

char *find_offset(struct wfs_inode *inode, off_t offset, int flag) {
    int block_index = offset / BLOCK_SIZE;
    off_t *block_pointer_array;
    
    if (block_index > D_BLOCK) {
        block_index -= IND_BLOCK;
        
        if (inode->blocks[IND_BLOCK] == 0) {
            off_t new_block = alloc_database();
            if (new_block < 0) {
                return NULL;
            }
            inode->blocks[IND_BLOCK] = new_block;
        }
        
        block_pointer_array = (off_t *)mmap_ptr(inode->blocks[IND_BLOCK]);
    } else {
        block_pointer_array = inode->blocks;
    }
    
    off_t target_block = *(block_pointer_array + block_index);
    
    if (target_block == 0 && flag) {
        target_block = alloc_database();
        if (target_block < 0 || (*(block_pointer_array + block_index) = target_block) == 0) {
            return_code = -ENOSPC;
            return NULL;
        }
    }

    return (char *)mmap_ptr(target_block) + (offset % BLOCK_SIZE);
}


int find_inode_path(char *path, struct wfs_inode **inode) {
    // Start from the root inode
    struct wfs_inode *current_inode = find_inode(0);
    if (!current_inode) {
        return -ENOENT;
    }

    // Skip leading '/'
    path++;

    // Parse the path iteratively
    while (*path) {
        char *path_temp = path;

        // Find the next '/' or end of string
        while (*path != '/' && *path != '\0') {
            path++;
        }

        // Null-terminate the current component
        if (*path != '\0') {
            *path = '\0';
            path++;
        }

        // Search for the entry in the current directory
        size_t size = current_inode->size;
        struct wfs_dentry *entry;
        int num = -1;

        for (off_t off = 0; off < size; off += sizeof(struct wfs_dentry)) {
            entry = (struct wfs_dentry *)find_offset(current_inode, off, 0);
            if (entry == NULL) {
                return_code = -ENOENT;
                return -1;
            }
            if (entry->num != 0 && !strcmp(entry->name, path_temp)) {
                num = entry->num;
                break;
            }
        }

        if (num < 0) {
            return_code = -ENOENT; // Path component not found
            return -1;
        }

        // Move to the next inode
        current_inode = find_inode(num);
        if (!current_inode) {
            return_code = -ENOENT; // Invalid inode
            return -1;
        }
    }

    // Assign the final inode
    *inode = current_inode;
    return 0;
}







// getattr: Retrieve file attributes for the given path
static int wfs_getattr(const char *path, struct stat *stbuf) {
    struct wfs_inode *inode;
    char *tmp_path = strdup(path);
    if (tmp_path == NULL){
        return -ENOMEM;
    }
    int result = find_inode_path(tmp_path, &inode);
    free(tmp_path);
    if (result < 0) {
        return return_code;
    }
    stbuf->st_mode = inode->mode;
    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_size = inode->size;
    stbuf->st_nlink = inode->nlinks;
    return 0;
}

// mknod: Create a file with the specified mode and device ID
static int wfs_mknod(const char *path, mode_t mode, dev_t rdev) {
    printf("mknod called for path: %s, mode: %o\n", path, mode);
    return -ENOSYS; // Operation not implemented
}

// mkdir: Create a directory with the specified mode
static int wfs_mkdir(const char *path, mode_t mode) {
        struct wfs_inode *parent = NULL;
    char *dir = strdup(path);
    char *file = strdup(path);

    if (!file || !dir) {
        free(dir);
        free(file);
        return -ENOMEM;
    }

    // Get parent directory inode
    if (find_inode_path(dirname(dir), &parent) < 0) {
        free(dir);
        free(file);
        return return_code;  // error code from find_inode_path
    }

    // Allocate a new inode for the directory
    struct wfs_inode *inode = alloc_inode();
    if (inode == NULL) {
        free(dir);
        free(file);
        return -ENOSPC;
    }

    // Set up the directory's inode
    inode->mode = S_IFDIR | mode;  // Set directory mode
    inode->uid = getuid();         // Set user ID
    inode->gid = getgid();         // Set group ID
    inode->size = 0;               // Directory size (initially 0)
    inode->nlinks = 2;             // '.' and '..' links for the directory

    // Directory entry (child directory entry in parent directory)
    struct wfs_dentry *entry;
    off_t offset = 0;
    bool entry_found = false;

    // Try to find an empty spot in the parent directory to add the new entry
    while (offset < parent->size) {
        entry = (struct wfs_dentry *)find_offset(parent, offset, 0);
        if (entry != NULL && entry->num == 0) {
            entry->num = inode->num;  // Link inode number to entry
            strncpy(entry->name, basename(file), MAX_NAME);  // Set the directory name
            parent->nlinks++;  // Increment parent directory's link count
            entry_found = true;
            break;
        }
        offset += sizeof(struct wfs_dentry);
    }

    // If no empty entry found, allocate new entry at the end of the parent directory
    if (!entry_found) {
        entry = (struct wfs_dentry *)find_offset(parent, parent->size, 1);
        if (entry == NULL) {
            free_inode(inode);  // Free the inode if no space is found
            free(dir);
            free(file);
            return -ENOSPC;
        }
        entry->num = inode->num;  // Link inode number to entry
        strncpy(entry->name, basename(file), MAX_NAME);  // Set the directory name
        parent->nlinks++;  // Increment parent directory's link count
        parent->size += sizeof(struct wfs_dentry);  // Increase parent directory size
    }

    // Clean up
    free(dir);
    free(file);

    return 0;  // Return success
}

// unlink: Remove a file
static int wfs_unlink(const char *path) {
    printf("unlink called for path: %s\n", path);
    return -ENOSYS; // Operation not implemented
}

// rmdir: Remove a directory
static int wfs_rmdir(const char *path) {
    printf("rmdir called for path: %s\n", path);
    return -ENOSYS; // Operation not implemented
}

// read: Read data from a file into a buffer
static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("read called for path: %s, size: %zu, offset: %ld\n", path, size, offset);
    return -ENOSYS; // Operation not implemented
}

// write: Write data to a file from a buffer
static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("write called for path: %s, size: %zu, offset: %ld\n", path, size, offset);
    return -ENOSYS; // Operation not implemented
}

// readdir: List the contents of a directory
static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("readdir called for path: %s\n", path);
    return -ENOSYS; // Operation not implemented
}

// Register the operations in the FUSE operations structure
static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod   = wfs_mknod,
    .mkdir   = wfs_mkdir,
    .unlink  = wfs_unlink,
    .rmdir   = wfs_rmdir,
    .read    = wfs_read,
    .write   = wfs_write,
    .readdir = wfs_readdir,
};

int main(int argc, char *argv[]) {
    struct stat tmp;
    int file_desc;
    char *disk_img = argv[1];
    for (int i = 2; i < argc; i++) {
        argv[i - 1] = argv[i];
    }
    argc = argc - 1;
    if ((file_desc = open(disk_img, O_RDWR)) < 0) {
        perror("Error opening file");
        exit(1);
    }
    if (fstat(file_desc, &tmp) < 0) {
        perror("Error getting file stats");
        exit(1);
    }
    mapped_region = mmap(NULL, tmp.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, file_desc, 0);
    if (mapped_region == MAP_FAILED) {
        perror("Error mapping memory");
        exit(1);
    }
    assert(find_inode(0) != NULL);
    munmap(mapped_region, tmp.st_size);
    close(file_desc);
    return fuse_main(argc, argv, &ops, NULL);
}
