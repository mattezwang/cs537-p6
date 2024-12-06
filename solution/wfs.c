// #define FUSE_USE_VERSION 30
// #include <fuse.h>
// #include <stdio.h>
// #include <string.h>
// #include <errno.h>
// #include <stdlib.h>
// #include <sys/mman.h>
// #include <unistd.h>
// #include <assert.h>
// #include <stdbool.h>
// #include <libgen.h>
// #include "wfs.h"

// void *mapped_region;
// int return_code;

// char* mmap_ptr(off_t offset) { 
//     return (char *)mapped_region + offset;
// }



// // allocate memory for a block
// size_t alloc_block(uint32_t *bitmap, size_t size) {

//     uint32_t temp_bitmap;
//     int bits = 32;
//     int max = UINT32_MAX;

//     for (size_t i = 0; i < size; i++) {
//         temp_bitmap = bitmap[i];

//         if(temp_bitmap == max) {
//             continue;
//         }

//         else {
//             for (uint32_t k = 0; k < bits; k++) {

//                 uint32_t temp = temp_bitmap & (0x1 << k);
//                 if (temp != 0) {
//                     continue;

//                 } else {
//                     bitmap[i] |= (0x1 << k);

//                     return (i * bits) + k;
//                 }
//             }
//         }
//     }

//     return -1;
// }


// off_t alloc_DB() {
//     int bits = 32;
//     struct wfs_sb *temp_mapped_region = (struct wfs_sb*) mapped_region;

//     char* a = (char *)mapped_region + temp_mapped_region->d_bitmap_ptr;
//     uint32_t b = (uint32_t *)a;
//     size_t c = temp_mapped_region -> num_data_blocks / bits
    
//     // postive
//     if(alloc_block(b, c) >= 0) {
//         return (BLOCK_SIZE * x) + temp_mapped_region -> d_blocks_ptr;
//     }

//     // if it's negative
//     else {
//         return_code = -ENOSPC;  
//         return -1;
//     }
// }



// struct wfs_inode *alloc_inode() {

//     struct wfs_sb *temp = (struct wfs_sb *) mapped_region;
//     off_t num = alloc_block((uint *) mmap_ptr(temp->i_bitmap_ptr), temp->num_inodes / 32);

//     if (num >= 0) {
//         struct wfs_inode *inode = (struct wfs_inode *)((char *)mmap_ptr(temp->i_blocks_ptr) + num * BLOCK_SIZE);
//         inode->num = num;
//         return inode;
//     }

//     else {
//         return_code = -ENOSPC;
//         return NULL;
//     }
// }



// void free_inode(struct wfs_inode *inode_to_free) {

//     memset(inode_to_free, 0, sizeof(struct wfs_inode));

//     char *inode_start = (char *)inode_to_free;
//     char *blocks_ptr = (char *)mmap_ptr(((struct wfs_sb *)mapped_region)->i_blocks_ptr);
//     off_t block_index = (inode_start - blocks_ptr) / BLOCK_SIZE;

//     // Access the bitmap and clear the corresponding bit
//     uint32_t *bitmap_ptr = (uint32_t *)mmap_ptr(((struct wfs_sb *)mapped_region)->i_bitmap_ptr);
//     bitmap_ptr[block_index / 32] &= ~(1U << (block_index % 32));
// }



// struct wfs_inode *find_inode(int num) {
//     // Get the inode bitmap pointer
//     struct wfs_sb *sb = (struct wfs_sb *)mapped_region;
//     uint *bitmap = (uint *)mmap_ptr(sb->i_bitmap_ptr);

//     // Check if the inode number is valid in the bitmap
//     int bitmap_index = num / 32;  // Index of the 32-bit integer in the bitmap
//     int bit_pos = num % 32; // Bit position within the 32-bit integer
//     int valid_inode = bitmap[bitmap_index] & (0x1 << bit_pos);

//     // If valid, calculate the inode's memory address and return it
//     if (valid_inode) {
//         char *base = (char *)mmap_ptr(sb->i_blocks_ptr);
//         return (struct wfs_inode *)(base + num * BLOCK_SIZE);
//     }

//     // Return NULL if the inode is invalid
//     return NULL;
// }

// char *find_offset(struct wfs_inode *inode, off_t offset, int flag) {
//     int block_index = offset / BLOCK_SIZE;
//     off_t *block_pointer_array;
    
//     if (block_index > D_BLOCK) {
//         block_index -= IND_BLOCK;
        
//         if (inode->blocks[IND_BLOCK] == 0) {
//             off_t new_block = alloc_DB();
//             if (new_block < 0) {
//                 return NULL;
//             }
//             inode->blocks[IND_BLOCK] = new_block;
//         }
        
//         block_pointer_array = (off_t *)mmap_ptr(inode->blocks[IND_BLOCK]);
//     } else {
//         block_pointer_array = inode->blocks;
//     }
    
//     off_t target_block = *(block_pointer_array + block_index);
    
//     if (target_block == 0 && flag) {
//         target_block = alloc_DB();
//         if (target_block < 0 || (*(block_pointer_array + block_index) = target_block) == 0) {
//             return_code = -ENOSPC;
//             return NULL;
//         }
//     }

//     return (char *)mmap_ptr(target_block) + (offset % BLOCK_SIZE);
// }


// int find_inode_path(char *path, struct wfs_inode **inode) {
//     // Start from the root inode
//     struct wfs_inode *current_inode = find_inode(0);
//     if (!current_inode) {
//         return -ENOENT;
//     }

//     // Skip leading '/'
//     path++;

//     // Parse the path iteratively
//     while (*path) {
//         char *path_temp = path;

//         // Find the next '/' or end of string
//         while (*path != '/' && *path != '\0') {
//             path++;
//         }

//         // Null-terminate the current component
//         if (*path != '\0') {
//             *path = '\0';
//             path++;
//         }

//         // Search for the entry in the current directory
//         size_t size = current_inode->size;
//         struct wfs_dentry *entry;
//         int num = -1;

//         for (off_t off = 0; off < size; off += sizeof(struct wfs_dentry)) {
//             entry = (struct wfs_dentry *)find_offset(current_inode, off, 0);
//             if (entry == NULL) {
//                 return_code = -ENOENT;
//                 return -1;
//             }
//             if (entry->num != 0 && !strcmp(entry->name, path_temp)) {
//                 num = entry->num;
//                 break;
//             }
//         }

//         if (num < 0) {
//             return_code = -ENOENT; // Path component not found
//             return -1;
//         }

//         // Move to the next inode
//         current_inode = find_inode(num);
//         if (!current_inode) {
//             return_code = -ENOENT; // Invalid inode
//             return -1;
//         }
//     }

//     // Assign the final inode
//     *inode = current_inode;
//     return 0;
// }







// // getattr: Retrieve file attributes for the given path
// static int my_getattr(const char *path, struct stat *stbuf) {
//     struct wfs_inode *inode;
//     char *tmp_path = strdup(path);
//     if (tmp_path == NULL){
//         return -ENOMEM;
//     }
//     int result = find_inode_path(tmp_path, &inode);
//     free(tmp_path);
//     if (result < 0) {
//         return return_code;
//     }
//     stbuf->st_size = inode->size;
//     stbuf->st_gid = inode->gid;
//     stbuf->st_nlink = inode->nlinks;
//     stbuf->st_mode = inode->mode;
//     stbuf->st_uid = inode->uid;
//     return 0;
// }


// // mknod: Create a file with the specified mode and device ID
// static int my_mknod(const char *path, mode_t mode, dev_t rdev) {
//     printf("mknod called for path: %s, mode: %o\n", path, mode);

//     // Step 1: Find the parent directory inode
//     struct wfs_inode *parent = NULL;
//     char *dir = strdup(path);
//     char *file = strdup(path);

//     if (!dir || !file) {
//         free(dir);
//         free(file);
//         return -ENOMEM; // Memory allocation error
//     }

//     // Get the parent directory inode
//     if (find_inode_path(dirname(dir), &parent) < 0) {
//         free(dir);
//         free(file);
//         return return_code; // Error code from find_inode_path
//     }

//     // Step 2: Allocate a new inode for the file
//     struct wfs_inode *inode = alloc_inode();
//     if (!inode) {
//         free(dir);
//         free(file);
//         return -ENOSPC; // No space left
//     }

//     // Initialize the inode with file information
//     inode->mode = S_IFREG | mode;  // Regular file mode
//     inode->uid = getuid();          // Set user ID
//     inode->gid = getgid();          // Set group ID
//     inode->size = 0;                // File size (initially 0)
//     inode->nlinks = 1;              // One link to the file (from parent directory)

//     // Step 3: Create a directory entry for the new file in the parent directory
//     struct wfs_dentry *entry = NULL;
//     off_t offset = 0;
//     bool entry_found = false;

//     while (offset < parent->size) {
//         entry = (struct wfs_dentry *)find_offset(parent, offset, 0);
//         if (entry != NULL && entry->num == 0) {  // Empty entry found
//             entry->num = inode->num;             // Link inode number to entry
//             strncpy(entry->name, basename(file), MAX_NAME);  // Set file name
//             parent->nlinks++;  // Increment parent directory's link count
//             entry_found = true;
//             break;
//         }
//         offset += sizeof(struct wfs_dentry);
//     }

//     // If no empty entry found, allocate new entry at the end of the parent directory
//     if (!entry_found) {
//         entry = (struct wfs_dentry *)find_offset(parent, parent->size, 1);
//         if (!entry) {
//             free_inode(inode);  // Free the inode if no space is found
//             free(dir);
//             free(file);
//             return -ENOSPC;  // No space left
//         }
//         entry->num = inode->num;  // Link inode number to entry
//         strncpy(entry->name, basename(file), MAX_NAME);  // Set file name
//         parent->nlinks++;  // Increment parent directory's link count
//         parent->size += sizeof(struct wfs_dentry);  // Increase parent directory size
//     }

//     // Clean up
//     free(dir);
//     free(file);

//     return 0;  // Success
// }


// // mkdir: Create a directory with the specified mode
// static int my_mkdir(const char *path, mode_t mode) {
//     struct wfs_inode *parent = NULL;
//     char *dir = strdup(path);
//     char *file = strdup(path);

//     if (!file || !dir) {
//         free(dir);
//         free(file);
//         return -ENOMEM;
//     }

//     // Get parent directory inode
//     if (find_inode_path(dirname(dir), &parent) < 0) {
//         free(dir);
//         free(file);
//         return return_code;  // error code from find_inode_path
//     }

//     // Allocate a new inode for the directory
//     struct wfs_inode *inode = alloc_inode();
//     if (inode == NULL) {
//         free(dir);
//         free(file);
//         return -ENOSPC;
//     }

//     // Set up the directory's inode
//     inode->mode = S_IFDIR | mode;  // Set directory mode
//     inode->uid = getuid();         // Set user ID
//     inode->gid = getgid();         // Set group ID
//     inode->size = 0;               // Directory size (initially 0)
//     inode->nlinks = 2;             // '.' and '..' links for the directory

//     // Directory entry (child directory entry in parent directory)
//     struct wfs_dentry *entry;
//     off_t offset = 0;
//     bool entry_found = false;

//     // Try to find an empty spot in the parent directory to add the new entry
//     while (offset < parent->size) {
//         entry = (struct wfs_dentry *)find_offset(parent, offset, 0);
//         if (entry != NULL && entry->num == 0) {
//             entry->num = inode->num;  // Link inode number to entry
//             strncpy(entry->name, basename(file), MAX_NAME);  // Set the directory name
//             parent->nlinks++;  // Increment parent directory's link count
//             entry_found = true;
//             break;
//         }
//         offset += sizeof(struct wfs_dentry);
//     }

//     // If no empty entry found, allocate new entry at the end of the parent directory
//     if (!entry_found) {
//         entry = (struct wfs_dentry *)find_offset(parent, parent->size, 1);
//         if (entry == NULL) {
//             free_inode(inode);  // Free the inode if no space is found
//             free(dir);
//             free(file);
//             return -ENOSPC;
//         }
//         entry->num = inode->num;  // Link inode number to entry
//         strncpy(entry->name, basename(file), MAX_NAME);  // Set the directory name
//         parent->nlinks++;  // Increment parent directory's link count
//         parent->size += sizeof(struct wfs_dentry);  // Increase parent directory size
//     }

//     // Clean up
//     free(dir);
//     free(file);

//     return 0;  // Return success
// }

// // unlink: Remove a file
// static int my_unlink(const char *path) {
//     printf("unlink called for path: %s\n", path);
//     return -ENOSYS; // Operation not implemented
// }

// // rmdir: Remove a directory
// static int my_rmdir(const char *path) {
//     printf("rmdir called for path: %s\n", path);
//     return -ENOSYS; // Operation not implemented
// }

// // read: Read data from a file into a buffer
// static int my_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
//     printf("read called for path: %s, size: %zu, offset: %ld\n", path, size, offset);
//     return -ENOSYS; // Operation not implemented
// }

// // write: Write data to a file from a buffer
// static int my_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
//     printf("write called for path: %s, size: %zu, offset: %ld\n", path, size, offset);
//     return -ENOSYS; // Operation not implemented
// }

// // readdir: List the contents of a directory
// static int my_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
//     printf("readdir called for path: %s\n", path);
//     return -ENOSYS; // Operation not implemented
// }

// // Register the operations in the FUSE operations structure
// static struct fuse_operations ops = {
//     .getattr = my_getattr,
//     .mknod   = my_mknod,
//     .mkdir   = my_mkdir,
//     .unlink  = my_unlink,
//     .rmdir   = my_rmdir,
//     .read    = my_read,
//     .write   = my_write,
//     .readdir = my_readdir,
// };

// int main(int argc, char *argv[]) {
//     struct stat tmp;
//     int file_desc;
//     char *disk_img = argv[1];
//     for (int i = 2; i < argc; i++) {
//         argv[i - 1] = argv[i];
//     }
//     argc = argc - 1;
//     if ((file_desc = open(disk_img, O_RDWR)) < 0) {
//         perror("Error opening file");
//         exit(1);
//     }
//     if (fstat(file_desc, &tmp) < 0) {
//         perror("Error getting file stats");
//         exit(1);
//     }
//     mapped_region = mmap(NULL, tmp.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, file_desc, 0);
//     if (mapped_region == MAP_FAILED) {
//         perror("Error mapping memory");
//         exit(1);
//     }
//     assert(find_inode(0) != NULL);
//     munmap(mapped_region, tmp.st_size);
//     close(file_desc);
//     return fuse_main(argc, argv, &ops, NULL);
// }




#include "wfs.h"
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <libgen.h>
#include <assert.h>
#include <stdint.h>
#define min(a, b) ((a) < (b) ? (a) : (b))

void *mapped_memory_region;
int err;

int get_inode(struct wfs_inode *enclosing, char *path, struct wfs_inode **inode);
int get_inode_path(char *path, struct wfs_inode **inode);
int wfs_mknod(const char *path, mode_t mode, dev_t dev);
int wfs_mkdir(const char *path, mode_t mode);
int wfs_getattr(const char *path, struct stat *stbuf);
int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int wfs_unlink(const char *path);
int wfs_rmdir(const char *path);
int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

char *find_offset(struct wfs_inode *inode, off_t off, int flag);
struct wfs_inode *find_inode(int num);

size_t allocate_block(uint32_t *bitmap, size_t size);
off_t allocate_DB();
struct wfs_inode *allocate_inode();

void free_bitmap(uint32_t pos, uint32_t *bitmap);
void free_db_block(off_t block);
void free_inode(struct wfs_inode *inode);

void change(struct stat *stbuf, struct wfs_inode *inode);
void update_size(struct wfs_inode *inode, off_t offset, size_t size);
char* mmap_ptr(off_t offset);

static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod = wfs_mknod,
    .mkdir = wfs_mkdir,
    .unlink = wfs_unlink,
    .rmdir = wfs_rmdir,
    .read = wfs_read,
    .write = wfs_write,
    .readdir = wfs_readdir,
};

int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
    (void)dev; 
    struct wfs_inode *parent = NULL;
    char *file = strdup(path);
    char *dir = strdup(path);
    if (!file || !dir) {
        free(dir);
        free(file);
        return -ENOMEM;
    }
    if (get_inode_path(dirname(dir), &parent) < 0) {
        free(dir);
        free(file);
        return err;
    }
    struct wfs_inode *inode = allocate_inode();
    if (inode == NULL) {
        free(dir);
        free(file);
        return -ENOSPC;  
    }
    inode->mode = __S_IFREG | mode;
    inode->uid = getuid();
    inode->gid = getgid();
    inode->size = 0;
    inode->nlinks = 1;
    struct wfs_dentry *entry;
    off_t offset = 0;
    bool check = false;
    while (offset < parent->size) {
        entry = (struct wfs_dentry *)find_offset(parent, offset, 0);
        if (entry != NULL) {
            if (entry->num == 0) {
                entry->num = inode->num;
                strncpy(entry->name, basename(file), MAX_NAME);
                parent->nlinks++;
                check = true;
                break;
            }
            offset = offset + sizeof(struct wfs_dentry);  
        } 
    }
    if (!check) {
        entry = (struct wfs_dentry *)find_offset(parent, parent->size, 1);
        if (entry != NULL) {
            entry->num = inode->num;
            strncpy(entry->name, basename(file), MAX_NAME);
            parent->nlinks++;
            parent->size = parent->size + sizeof(struct wfs_dentry);
        }
        else{
            free_inode(inode);
            free(dir);
            free(file);
            return -ENOSPC;
        }  
    }
    free(dir);
    free(file);
    return 0;
}

int wfs_mkdir(const char *path, mode_t mode) {
    struct wfs_inode *parent = NULL;
    char *dir = strdup(path);
    char *file = strdup(path);
    if(!file || !dir){
        free(dir);
        free(file);
        return -ENOMEM;
    }
    if (get_inode_path(dirname(dir), &parent) < 0) {
        free(dir);
        free(file);
        return err;
    }
    struct wfs_inode *inode = allocate_inode();
    if (inode == NULL) {
        free(dir);
        free(file);
        return -ENOSPC;
    }
    inode->mode = __S_IFDIR | mode;
    inode->uid = getuid();
    inode->gid = getgid();
    inode->size = 0;
    inode->nlinks = 2;
    struct wfs_dentry *entry;
    off_t offset = 0;
    bool check = false;
    while (offset < parent->size) {
        entry = (struct wfs_dentry *)find_offset(parent, offset, 0);
        if (entry != NULL) {
            if (entry->num == 0) {
            entry->num = inode->num;
            strncpy(entry->name, basename(file), MAX_NAME);
            parent->nlinks++;
            check = true;
            break;
        }
        }
        offset = offset + sizeof(struct wfs_dentry);
    }
    if (!check) {
        entry = (struct wfs_dentry *)find_offset(parent, parent->size, 1);
        if (entry == NULL) {
            free_inode(inode);
            free(dir);
            free(file);
            return -ENOSPC;
        }
        entry->num = inode->num;
        strncpy(entry->name, basename(file), MAX_NAME);
        parent->nlinks++;
        parent->size = parent->size + sizeof(struct wfs_dentry);
    }
    free(dir);
    free(file);
    return 0;
}

int wfs_getattr(const char *path, struct stat *stbuf) {
    struct wfs_inode *inode;
    char *temp = strdup(path);
    if (get_inode_path(temp, &inode) < 0){
        free(temp);
        return err;
    }
    change(stbuf, inode);
    free(temp);
    return 0;
}

void change(struct stat *stbuf, struct wfs_inode *inode) {
    stbuf->st_mode = inode->mode;
    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_size = inode->size;
    stbuf->st_nlink = inode->nlinks;
}


int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
    (void)fi;
    struct wfs_inode *inode;
    char *search = strdup(path);
    if (get_inode_path(search, &inode) < 0){
        free(search);
        return err;
    }
    size_t read = 0;
    for (size_t i = 0; read < size && offset + read < inode->size; i++){
            size_t curr = offset + read;
            size_t block = BLOCK_SIZE - (curr % BLOCK_SIZE);
            size_t remaining = inode->size - curr;
            size_t read_now;
            if (block > remaining) {
                read_now = remaining;
            } else {
                read_now = block;
            }
            char *addr = find_offset(inode, curr, 0);
            if(addr == NULL) {
                return -ENOENT;
            }
            memcpy(buf + read, addr, read_now);
            read = read + read_now;
    }
    free(search);
    return read;
}

int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){
    (void)fi;
    struct wfs_inode *inode;
    char *search = strdup(path);
    if (get_inode_path(search, &inode) < 0){
        free(search);
        return err;
    }
    size_t write = 0;
    size_t curr = offset;
    size_t write_now = 0;

    for (; write < size; write = write + write_now, curr = curr + write_now){
        size_t block = BLOCK_SIZE - (curr % BLOCK_SIZE);
        write_now = min(block, size - write);
        char *addr = find_offset(inode, curr, 1);
        if (addr == NULL){
            free(search);
            return -ENOSPC; 
        }
        memcpy(addr, buf + write, write_now);
    }
    update_size(inode, offset, size);
    free(search);
    return write;
}

void update_size(struct wfs_inode *inode, off_t offset, size_t size) {
    if (offset + size > inode->size) {
        inode->size = offset + size;
    }
}

int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){
    (void)fi;
    (void)offset;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    struct wfs_inode *inode;
    char *search = strdup(path);
    if (get_inode_path(search, &inode) < 0) {
        free(search);
        return err;
    }
    size_t size = inode->size;
    struct wfs_dentry *entry;
    off_t offset_temp = 0;
    while (offset_temp < size) {
        entry = (struct wfs_dentry *)find_offset(inode, offset_temp, 0);
        if (entry != NULL) {
            if (entry->num != 0) {
                filler(buf, entry->name, NULL, 0);
            }
        }
        offset_temp = offset_temp + sizeof(struct wfs_dentry);
    }
    free(search);
    return 0;
}

int wfs_unlink(const char *path) {
    struct wfs_inode *parent;
    struct wfs_inode *inode;
    char *path_copy = strdup(path);
    char *search = strdup(path);
    if (get_inode_path(dirname(path_copy), &parent) < 0) {
        free(path_copy);
        free(search);
        return err;
    }
    if (get_inode_path(search, &inode) < 0) {
        free(path_copy);
        free(search);
        return err;
    }
    size_t size = parent->size;
    struct wfs_dentry *entry;
    off_t offset = 0;
    while (offset < size) {
        entry = (struct wfs_dentry *)find_offset(parent, offset, 0);
        if(entry == NULL) {
            return -ENOENT;
        }
        if (entry->num == inode->num) {
            entry->num = 0;
            break;
        }
        offset = offset + sizeof(struct wfs_dentry);
    }
    for (int i = 0; i < D_BLOCK; i++) {
        if (inode->blocks[i] != 0) {
            free_db_block(inode->blocks[i]);
            inode->blocks[i] = 0;
        }
    }
    free_inode(inode);
    free(path_copy);
    free(search);
    return 0;
}

int wfs_rmdir(const char *path) {
    wfs_unlink(path);
    return 0;
}

char *find_offset(struct wfs_inode *inode, off_t offset, int flag) {
    int index = offset / BLOCK_SIZE;
    off_t *arr;
    if (index > D_BLOCK) {
        index = index - IND_BLOCK;
        if (inode->blocks[IND_BLOCK] == 0) {
            if ((inode->blocks[IND_BLOCK] = allocate_DB()) < 0) {
                return NULL;
            }
        }
        arr = (off_t *)mmap_ptr(inode->blocks[IND_BLOCK]);
    }
    else {
        arr = inode->blocks;
    }
    if (*(arr + index) == 0 && flag) {   
        off_t block = allocate_DB();
        if(block < 0 || (*(arr + index) = block) == 0) {
            err = -ENOSPC;
            return NULL;
        }
    }
    return (char *)mmap_ptr(*(arr + index)) + (offset % BLOCK_SIZE);
}


char* mmap_ptr(off_t offset) { 
    return (char *)mapped_memory_region + offset;
}

void free_bitmap(uint32_t pos, uint32_t *bitmap) {
    bitmap[pos / 32] -= 1 << (pos % 32);
}

void free_db_block(off_t block) {
    memset(mmap_ptr(block), 0, BLOCK_SIZE);
    free_bitmap((block - ((struct wfs_sb *)mapped_memory_region)->d_blocks_ptr) / BLOCK_SIZE, (uint32_t *)mmap_ptr(((struct wfs_sb *)mapped_memory_region)->d_bitmap_ptr));
}

void free_inode(struct wfs_inode *inode) {
    memset(inode, 0, sizeof(struct wfs_inode));
    free_bitmap(((char*)inode - (char*)mmap_ptr(((struct wfs_sb *)mapped_memory_region)->i_blocks_ptr)) / BLOCK_SIZE, (uint32_t *)mmap_ptr(((struct wfs_sb *)mapped_memory_region)->i_bitmap_ptr));
}

struct wfs_inode *find_inode(int num) {
    uint32_t *bitmap = (uint32_t *)mmap_ptr(((struct wfs_sb *)mapped_memory_region)->i_bitmap_ptr);
    return (bitmap[num / 32] & (0x1 << (num % 32)))  ? (struct wfs_inode *)((char *)mmap_ptr(((struct wfs_sb *)mapped_memory_region)->i_blocks_ptr) + num * BLOCK_SIZE) : NULL;
}

size_t allocate_block(uint32_t *bitmap, size_t size) {
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

off_t allocate_DB() {
    struct wfs_sb *temp = (struct wfs_sb *)mapped_memory_region;
    off_t num = allocate_block((uint32_t *)mmap_ptr(temp->d_bitmap_ptr), temp->num_data_blocks / 32);
    if (num < 0) {
        err = -ENOSPC;  
        return -1;
    }
    return temp->d_blocks_ptr + BLOCK_SIZE * num;
}

struct wfs_inode *allocate_inode() {
    struct wfs_sb *temp = (struct wfs_sb *)mapped_memory_region;
    off_t num = allocate_block((uint32_t *)mmap_ptr(temp->i_bitmap_ptr), temp->num_inodes / 32);
    if (num < 0) {
        err = -ENOSPC;
        return NULL;
    }
    struct wfs_inode *inode = (struct wfs_inode *)((char *)mmap_ptr(temp->i_blocks_ptr) + num * BLOCK_SIZE);
    inode->num = num;
    return inode;
}

int get_inode(struct wfs_inode *enclosing, char *path, struct wfs_inode **inode) {
    if (!strcmp(path, "")) {
        *inode = enclosing;
        return 0;
    }
    char *path_temp = path;
    while (*path != '/' && *path != '\0') {
        path++;
    }
    if (*path != '\0') {
        *path = '\0';
        path++;
    }
    size_t size = enclosing->size;
    struct wfs_dentry *entry;
    int num = -1;
    for (off_t off = 0; off < size; off += sizeof(struct wfs_dentry)) {
        entry = (struct wfs_dentry *)find_offset(enclosing, off, 0);
        if(entry == NULL) {
            err = -ENOENT;
            return -1;
        }
        if (entry->num != 0 && !strcmp(entry->name, path_temp)) {
            num = entry->num;
            break;
        }
    }
    if (num < 0) {
        err = -ENOENT;
        return -1;
    }
    return get_inode(find_inode(num), path, inode);
}

int get_inode_path(char *path, struct wfs_inode **inode) {
    return get_inode(find_inode(0), path + 1, inode);
}

int main(int argc, char *argv[]) {
    int status;
    struct stat temp;
    int fd;
    char *disk_img = argv[1];
    for (int i = 2; i < argc; i++) {
        argv[i - 1] = argv[i];
    }
    argc = argc - 1;
    if ((fd = open(disk_img, O_RDWR)) < 0) {
        perror("Error opening file");
        exit(1);
    }
    if (fstat(fd, &temp) < 0) {
        perror("Error getting file stats");
        exit(1);
    }
    mapped_memory_region = mmap(NULL, temp.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_memory_region == MAP_FAILED) {
        perror("Error mapping memory");
        exit(1);
    }
    assert(find_inode(0) != NULL);
    status = fuse_main(argc, argv, &ops, NULL);
    munmap(mapped_memory_region, temp.st_size);
    close(fd);
    return status;
}