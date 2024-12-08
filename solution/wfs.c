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
#include <time.h>


void *disk_maps[10];
int num_disks;

static int wfs_getattr(const char *path, struct stat *stbuf);
static int wfs_mknod(const char *path, mode_t mode, dev_t rdev);
static int wfs_mkdir(const char *path, mode_t mode);
static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int wfs_unlink(const char *path);
static int wfs_rmdir(const char *path);

static struct fuse_operations ops = {
    .getattr = wfs_getattr,
    .mknod = wfs_mknod,
    .mkdir = wfs_mkdir,
    .readdir = wfs_readdir,
    .read = wfs_read,
    .write = wfs_write,
    .unlink = wfs_unlink,
    .rmdir = wfs_rmdir
};







struct wfs_inode *lookup_inode_recursive(const char *fp, char *disk, struct wfs_inode *current_inode) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    char* disk_map = (char *)disk_maps[0];
    
    // this is our base case
    if (strcmp(fp, "/") == 0 || *fp == '\0') {
        return current_inode;
    }

    // gets the first parts of the path, before the "/"
    const char *first_part = strchr(fp, '/');
    size_t first_part_length;

    if(first_part == NULL) {
        first_part_length = strlen(fp);
    } else {
        first_part_length = (size_t)(first_part) - (size_t)(fp);
    }

    // max filename length is 28 (from write up)
    char token[28];
    strncpy(token, fp, first_part_length);
    token[first_part_length] = '\0';


    struct wfs_dentry *entry;
    int i = 0;

    // thorugh the blocks
    while (i < D_BLOCK && current_inode -> blocks[i] > 0) {

        entry = (struct wfs_dentry *)(disk_map + current_inode->blocks[i]);

        // inside the entry now
        int block_num;
        int checker;
        for (int j = 0; j < BLOCK_SIZE; j+= sizeof(struct wfs_dentry)) {
            block_num = j/sizeof(struct wfs_dentry);
            checker = strcmp(entry[block_num].name, token);

            if(checker != 0) {
                continue;
            }
            else {
                struct wfs_inode *next_inode = (struct wfs_inode *)(disk + superblock->i_blocks_ptr + (entry[block_num].num * BLOCK_SIZE));
                if(first_part == NULL) {
                    return lookup_inode_recursive("", disk, next_inode);
                }
                else {
                    return lookup_inode_recursive(first_part + 1, disk, next_inode);
                } 
            }
        }

        i++;
    }

    // don't think it should ever reach this point
    return NULL;
}



// wrapper for the helper basically
struct wfs_inode *lookup_inode(const char *fp, char *disk) {
    
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    struct wfs_inode *root_inode = (struct wfs_inode *)(disk + superblock->i_blocks_ptr);

    const char *param = fp;
    if(fp[0] == '/') {
        param = fp+1;
    }

    return lookup_inode_recursive(param, disk, root_inode);
}



// helper function to find free inode in block
int find_free_inode(char *bitmap_ptr, int num_inodes) {
    int byte = 8;
    // -1 = not found, other = found
    int found = -1;

    for (int i = 0; i < num_inodes; i++) {
        int bitmap_byte = i / byte;
        int bitmap_bit = i % byte;
        if ((bitmap_ptr[bitmap_byte] & (1 << bitmap_bit))) {
            continue;
        } else {
            found = i;
            break;
        }
    }
    return found;
}




// allocate inode after finding free inode
int alloc_inode(char *disk) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    char *inode_bitmap_ptr = disk + superblock->i_bitmap_ptr;;
    int inode;
    int byte = 8;
    int available;

    switch (superblock -> raid_mode) {
        case 0: {

            // i = inode num
            for (int i = 0; i < superblock->num_inodes; i++) {
                int bitmap_byte = i / byte;
                int bitmap_bit = i % byte;

                available = 1;

                // j = disk_num
                for (int j = 0; j < num_disks; j++) {
                    inode_bitmap_ptr = (char *)disk_maps[j] + superblock -> i_bitmap_ptr;

                    // checking if it is already allocated or not
                    if (inode_bitmap_ptr[bitmap_byte] & (1 << bitmap_bit)) { 
                        available = 0;
                        break;
                    }
                }

                if (available) {
                    
                    for (int j = 0; j < num_disks; j++) {
                        inode_bitmap_ptr = (char *)disk_maps[j] + superblock->i_bitmap_ptr;
                        inode_bitmap_ptr[bitmap_byte] |= (1 << bitmap_bit);
                    }
                    return i;
                }
            }

            // break because switches need it
            break;
        }

        default: {

            inode = find_free_inode(inode_bitmap_ptr, superblock->num_inodes);

            if(inode < 0) {
                break;
            }
            else {
                inode_bitmap_ptr[inode / byte] |= (1 << (inode % byte));
            }

            // break because switches need it
            break;
        }
    }
    
    if (inode < 0) {
        inode = -ENOSPC;
    }
    return inode;
}







// Helper function to zero out a block
void clear_block(char *block_address) {

    // don't even need to set it to anything
    char zeroed_block[BLOCK_SIZE];
    memset(zeroed_block, 0, BLOCK_SIZE);

    memcpy(block_address, zeroed_block, BLOCK_SIZE);
}

// Function to allocate a block
int alloc_block(char *disk) {

    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    char *data_bitmap_char = disk + superblock->d_bitmap_ptr;
    int byte = 8;

    switch (superblock->raid_mode) {

        case 0: {
            // iterate thorugh all data blocks
            for (int i = 0; i < superblock->num_data_blocks; i++) {

                // disk for curr block
                int current_disk = i % num_disks;
                uint8_t *data_bitmap = (uint8_t *)disk_maps[current_disk] + superblock -> d_bitmap_ptr;

                // block num in disk (as name says)
                int block_num_in_disk = i / num_disks;

                int bitmap_byte = block_num_in_disk / byte;
                int bitmap_bit = block_num_in_disk % byte;


                if((data_bitmap[bitmap_byte] & (1 << bitmap_bit))) {
                    continue;
                }

                else {
                    data_bitmap[bitmap_byte] |= (1 << bitmap_bit);

                    // Zero out the newly allocated block
                    char *block_address = (char *)disk_maps[current_disk] + superblock-> d_blocks_ptr + (block_num_in_disk * BLOCK_SIZE);

                    clear_block(block_address);
                    return i;
                }
            }
            break;
        }
        
        case 1:
        default: {
            for (int i = 0; i < superblock ->num_data_blocks; i++) {
                if((data_bitmap_char[i / byte] & (1 << (i % byte)))) {
                    continue;
                }

                else {
                    data_bitmap_char[i / byte] |= (1 << (i % byte));
                    char *block_address = disk + (i * BLOCK_SIZE) + superblock -> d_blocks_ptr;
                    clear_block(block_address);
                    return i;
                }
            }

            break;
        }
    }

    // if i is not returned, that means we did not have a free block available
    return -ENOSPC;
}





static int wfs_getattr(const char *path, struct stat *stbuf) {

    int rc = 0;
    size_t size = sizeof(struct stat);
    memset(stbuf, 0, size);

    struct wfs_inode *inode = lookup_inode(path, (char *)disk_maps[0]);

    if (!inode) {
        rc = -ENOENT;
    } else {
        stbuf->st_mode = inode->mode;    // File mode
        stbuf->st_nlink = inode->nlinks; // Number of links
        stbuf->st_uid = inode->uid;      // User ID
        stbuf->st_gid = inode->gid;      // Group ID
        stbuf->st_size = inode->size;    // File size
        stbuf->st_atime = inode->atim;   // Last access time
        stbuf->st_mtime = inode->mtim;   // Last modification time
        stbuf->st_ctime = inode->ctim;   // Last status change time
    }

    return rc;
}



// helper for the new inode
int set_new_inode(mode_t mode, struct wfs_inode *new_inode, int new_inode_index) {

    memset(new_inode, 0, BLOCK_SIZE);

    new_inode->num = new_inode_index;
    new_inode->mode = S_IFDIR | mode;
    new_inode->size = 0;

    new_inode->uid = getuid();
    new_inode->gid = getgid();

    new_inode->nlinks = 2;
    new_inode->atim = new_inode->mtim = new_inode->ctim = time(NULL);

    return 0;
}


int wfs_mkdir_helper_raid_mode(mode_t mode, int new_inode_index, char *disk, const char *parent_path) {
    struct wfs_sb *superblock = (struct wfs_sb *) disk_maps[0];
    char *inode_table;
    struct wfs_inode *inode;

    switch (superblock -> raid_mode) {
        case 0: {

            for (int i = 0; i < num_disks; i++) {

                inode_table = ((char *) disk_maps[i] + superblock -> i_blocks_ptr);
                inode = (struct wfs_inode *)((new_inode_index * BLOCK_SIZE) + inode_table);

                if (set_new_inode(mode, inode, new_inode_index) != 0) {
                    return -1;
                }
                else {
                    // printf("this successfully worked in wfs_mkdir_helper_raid_mode case 0 sdfasf\n");
                    continue;
                }
            }

            break;
        }

        case 1: {
            inode_table = (disk + superblock->i_blocks_ptr);
            inode = (struct wfs_inode *)(inode_table + (new_inode_index * BLOCK_SIZE));

            if (set_new_inode(mode, inode, new_inode_index) != 0) {
                return -1;
            }

            else {
                // printf("this successfully worked in wfs_mkdir_helper_raid_mode case 1 sdafmsa\n");
                break;
            }
        }

        default: {

            printf("should never reach here, needs to be either raid mode 0 or 1\n");
            return -EINVAL;
        }
    }

    // everything worked
    return 0;
}




#include <libgen.h>  // For basename() and dirname()

int wfs_mkdir_helper(const char *path, mode_t mode, char *disk) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];

    // Copy path to mutable buffers for basename() and dirname()
    char path_copy1[28];
    strncpy(path_copy1, path, sizeof(path_copy1));
    char *parent_path = dirname(path_copy1);

    struct wfs_inode *parent_inode = lookup_inode(parent_path, disk);
    if (!parent_inode) {
        return -ENOENT;  // Parent directory does not exist
    }

    char path_copy2[28];
    strncpy(path_copy2, path, sizeof(path_copy2));
    char *new_name = basename(path_copy2);

    // Allocate a new inode for the directory
    int new_inode_index = alloc_inode(disk);
    if (new_inode_index < 0) {
        return new_inode_index;  // Propagate ENOSPC
    }
    printf("New inode index: %d\n", new_inode_index);

    // Delegate RAID mode handling to helper function
    if (wfs_mkdir_helper_raid_mode(mode, new_inode_index, disk, parent_path) != 0) {
        return -1;  // RAID mode handling failed
    }

    // Add the new directory entry to the parent directory
    int found_space = 0;
    printf("Parent inode num: %d\n", parent_inode->num);
    for (int i = 0; i < D_BLOCK; i++) {
        if (parent_inode->blocks[i] == 0) {
            // Allocate a new block for the parent directory
            printf("CALLING ALLOCATE BLOCK\n");
            int block_index = alloc_block(disk);
            if (block_index < 0) {
                return block_index;  // Propagate ENOSPC
            }
            printf("Allocating data block\n");
            if (superblock->raid_mode == 0) {
                for (int d = 0; d < num_disks; d++) {
                    int logical_block_num = block_index / num_disks;
                    struct wfs_inode *sync_inode = lookup_inode(parent_path, (char *)disk_maps[0]);
                    sync_inode->blocks[i] = superblock->d_blocks_ptr + logical_block_num * BLOCK_SIZE;
                }
            } else {
                parent_inode->blocks[i] = superblock->d_blocks_ptr + block_index * BLOCK_SIZE;
            }
        }

        // Add the new directory entry in the block
        struct wfs_dentry *dir_entries = (struct wfs_dentry *)(disk + parent_inode->blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (dir_entries[j].num == 0) {  // Empty entry
                strncpy(dir_entries[j].name, new_name, MAX_NAME);
                dir_entries[j].num = new_inode_index;
                parent_inode->size += sizeof(struct wfs_dentry);
                found_space = 1;
                break;
            }
        }

        if (found_space) {
            break;
        }
    }

    if (!found_space) {
        return -ENOSPC;
    }

    return 0;  // Success
}





static int wfs_mkdir(const char *path, mode_t mode) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];

	if(superblock->raid_mode == 0) {
		wfs_mkdir_helper(path, mode, (char *)disk_maps[0]);
	} else {
	for(int i = 0; i < num_disks; i++) {
		wfs_mkdir_helper(path, mode, (char *)disk_maps[i]);
	}	
	}
	return 0;
}

static int wfs_mknod_helper(const char *path, mode_t mode, char *disk) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];

    char parent_path[1024], new_name[MAX_NAME];
    strncpy(parent_path, path, sizeof(parent_path));

    // LAST occurence of the thing in the tring
    char *slash = strrchr(parent_path, '/');
    if (!slash || slash == parent_path) {
        strncpy(new_name, path + 1, sizeof(new_name));
        strcpy(parent_path, "/");
    } else {
        strncpy(new_name, slash + 1, sizeof(new_name));
        *slash = '\0';
    }

    struct wfs_inode *parent_inode = lookup_inode(parent_path, disk);
    if (!parent_inode) {
        return -ENOENT;  // Parent directory not found
    }

    int new_inode_index = alloc_inode(disk);
    if (new_inode_index < 0) {
        return -ENOSPC;  // No free inodes
    }

    if(superblock->raid_mode == 0) {
    	for(int i = 0; i < num_disks; i++) {
                char *inode_table = ((char *)disk_maps[i] + superblock->i_blocks_ptr);
                struct wfs_inode *new_inode =(struct wfs_inode *) (inode_table + (new_inode_index * BLOCK_SIZE));
                printf("allocating inode at index: %d, disk: %d\n", new_inode_index, i);

                memset(new_inode, 0, BLOCK_SIZE);
                new_inode->num = new_inode_index;
                new_inode->mode = S_IFREG | mode; // Set directory mode
                new_inode->nlinks = 1;           // "." and parent's link
                new_inode->size = 0;             // Empty directory initially
                new_inode->atim = new_inode->mtim = new_inode->ctim = time(NULL);
                new_inode->uid = getuid();
                new_inode->gid = getgid();

        }

    } else {
    	char *inode_table = disk + superblock->i_blocks_ptr;
    	struct wfs_inode *new_inode = (struct wfs_inode *)(inode_table + (new_inode_index * BLOCK_SIZE));
    	memset(new_inode, 0, BLOCK_SIZE);
    	new_inode->num = new_inode_index;
    	new_inode->mode = S_IFREG | mode;
    	new_inode->nlinks = 1;
    	new_inode->size = 0;
    	new_inode->atim = new_inode->mtim = new_inode->ctim = time(NULL);
    	new_inode->uid = getuid();
    	new_inode->gid = getgid();

    }
    // Add directory entry in parent
    int found_space = 0;
    for (int i = 0; i < D_BLOCK; i++) {
        if (parent_inode->blocks[i] == 0) {
            int block_index = alloc_block(disk);
            if (block_index < 0) {
                return -ENOSPC;  // No free blocks
            }
            parent_inode->blocks[i] = superblock->d_blocks_ptr + block_index * BLOCK_SIZE;
        }
        struct wfs_dentry *dir_entries = (struct wfs_dentry *)(disk + parent_inode->blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (dir_entries[j].num == 0) {
                strncpy(dir_entries[j].name, new_name, MAX_NAME);
                dir_entries[j].num = new_inode_index;
                parent_inode->size += sizeof(struct wfs_dentry);
                found_space = 1;
                break;
            }
        }
        if (found_space) {
            break;
        }
    }

    if (!found_space) {
        return -ENOSPC;  // Parent directory full
    }

    return 0;  // Success
}

static int wfs_mknod(const char *path, mode_t mode, dev_t rdev) {

    (void)rdev;
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];

    int result = 0;
    if(superblock->raid_mode == 0) {
    	result = wfs_mknod_helper(path, mode, (char *)disk_maps[0]);
    } else {
    for (int i = 0; i < num_disks; i++) {
        result = wfs_mknod_helper(path, mode, (char *)disk_maps[i]);
        if (result < 0) {
            // Rollback any partial allocations
            for (int j = 0; j <= i; j++) {
                char *disk = (char *)disk_maps[j];
                char *inode_bitmap = disk + superblock->i_bitmap_ptr;
                int inode_num = alloc_inode(disk);
                if (inode_num >= 0) {
                    inode_bitmap[inode_num / 8] &= ~(1 << (inode_num % 8));  // Free the inode
                }
            }
            return result;  // Propagate error
        }
    }
    }
    return result;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("readdir called for path: %s\n", path);

    // Find the inode for the directory
    struct wfs_inode *inode = lookup_inode(path, (char *)disk_maps[0]);
    if (!inode) {
        return -ENOENT; // Directory not found
    }

    // Ensure the inode is a directory
    if (!S_ISDIR(inode->mode)) {
        return -ENOTDIR; // Not a directory
    }

    // Add "." and ".." entries
    filler(buf, ".", NULL, 0); // Current directory
    filler(buf, "..", NULL, 0); // Parent directory

    // Iterate through the blocks of the directory inode
    for (int i = 0; i < D_BLOCK; i++) {
        if (inode->blocks[i] == 0) break; // No more blocks

        // Access directory entries stored in the block
        struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)disk_maps[0] + inode->blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (dir_entries[j].num != 0) { // Valid entry
                filler(buf, dir_entries[j].name, NULL, 0); // Add entry to the buffer
                printf("Adding entry: %s\n", dir_entries[j].name);
            }
        }
    }

    return 0; // Success
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    // Find the inode for the file
    struct wfs_inode *inode = lookup_inode(path, (char *)disk_maps[0]);
    if (!inode) {
        return -ENOENT; // File not found
    }

    if (!S_ISREG(inode->mode)) {
        return -EISDIR; // Cannot read a directory
    }

    size_t bytes_read = 0;               // Track bytes read
    size_t block_offset = offset / BLOCK_SIZE;  // Determine starting block
    size_t block_start_offset = offset % BLOCK_SIZE; // Offset within the block

    while (bytes_read < size && block_offset < D_BLOCK + (BLOCK_SIZE / sizeof(uint32_t))) {
        void *data_block = NULL;

        // Handle direct blocks
        if (block_offset < D_BLOCK) {
            if (inode->blocks[block_offset] == 0) {
                break; // No more data
            }
            data_block = (char *)disk_maps[0] + inode->blocks[block_offset];
        }
        // Handle indirect blocks
        else {
            size_t indirect_offset = block_offset - D_BLOCK;

            if (inode->blocks[IND_BLOCK] == 0) {
                break; // No indirect blocks
            }

            uint32_t *indirect_block = (uint32_t *)((char *)disk_maps[0] + inode->blocks[IND_BLOCK]);
            if (indirect_block[indirect_offset] == 0) {
                break; // No more data
            }
            data_block = (char *)disk_maps[0] + indirect_block[indirect_offset];
        }

        // Read from the block
        size_t block_available_space = BLOCK_SIZE - block_start_offset;
        size_t bytes_to_read = (size - bytes_read < block_available_space)
                                   ? size - bytes_read
                                   : block_available_space;

        memcpy(buf + bytes_read, (char *)data_block + block_start_offset, bytes_to_read);
        bytes_read += bytes_to_read;
        block_offset++;
        block_start_offset = 0; // Reset offset for subsequent blocks
    }

    return bytes_read; // Return the total number of bytes read
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    struct wfs_inode *inode = lookup_inode(path, (char *)disk_maps[0]);
    if (!inode) {
        return -ENOENT; // File not found
    }

    if (!S_ISREG(inode->mode)) {
        return -EISDIR; // Cannot write to a directory
    }

    size_t bytes_written = 0;               // Track how many bytes are written
    size_t block_offset = offset / BLOCK_SIZE;  // Determine starting block
    size_t block_start_offset = offset % BLOCK_SIZE; // Offset within the block

    while (bytes_written < size) {
	int disk_index, logical_block_num;

        if (superblock->raid_mode == 0) {
            // RAID 0 Striping
            disk_index = block_offset % num_disks;      // Determine disk for this block
            logical_block_num = block_offset / num_disks; // Logical block on the selected disk
        } else {
            // Non-RAID or RAID 1 (Mirroring)
            disk_index = 0;          // Default to the first disk
            logical_block_num = block_offset; // Logical block matches the block offset
        }

        if (block_offset < D_BLOCK) {
            // **Handle Direct Blocks**
            if (inode->blocks[block_offset] == 0) {
                // Allocate a new block
                int new_block = alloc_block((char *)disk_maps[disk_index]);
                if (new_block < 0) {
                    return -ENOSPC; // No space available
                }

		if(superblock->raid_mode == 0) {
			for (int i = 0; i < num_disks; i++) {
			    char *disk = (char *)disk_maps[i];
                            struct wfs_inode *mirror_inode = (struct wfs_inode *)(disk + superblock->i_blocks_ptr + inode->num * BLOCK_SIZE);
                            mirror_inode->blocks[logical_block_num] = superblock->d_blocks_ptr + new_block * BLOCK_SIZE;
                        }
		} else {
                	// Mirror the allocation across all disks
                	for (int i = 0; i < num_disks; i++) {
                	    char *disk = (char *)disk_maps[i];
                	    char *data_bitmap = disk + superblock->d_bitmap_ptr;
                	    data_bitmap[new_block / 8] |= (1 << (new_block % 8));

                	    struct wfs_inode *mirror_inode = (struct wfs_inode *)(disk + superblock->i_blocks_ptr + inode->num * BLOCK_SIZE);
                	    mirror_inode->blocks[block_offset] = superblock->d_blocks_ptr + new_block * BLOCK_SIZE;
                	}
		}
            }

            // Write data to the block
            size_t block_available_space = BLOCK_SIZE - block_start_offset;
            size_t bytes_to_write = (size - bytes_written < block_available_space)
                                        ? size - bytes_written
                                        : block_available_space;

	    if(superblock->raid_mode == 0) {
                void *block_ptr = (char *)disk_maps[disk_index] + inode->blocks[logical_block_num] + block_start_offset;
                memcpy(block_ptr, buf + bytes_written, bytes_to_write);
	    } else {
            	for (int i = 0; i < num_disks; i++) {
            	    char *disk = (char *)disk_maps[i];
            	    void *block_ptr = (char *)disk + inode->blocks[block_offset] + block_start_offset;
            	    memcpy(block_ptr, buf + bytes_written, bytes_to_write);
            	}
	    }

            bytes_written += bytes_to_write;
            block_offset++;
            block_start_offset = 0; // Reset block offset for subsequent blocks
        } else {
            // **Handle Indirect Blocks**
            size_t indirect_offset = block_offset - D_BLOCK;

            if (inode->blocks[IND_BLOCK] == 0) {

                // Allocate an indirect block
                int indirect_block_index = alloc_block((char *)disk_maps[disk_index]);
                if (indirect_block_index < 0) {
                    return -ENOSPC; // No space available
                }

		if(superblock->raid_mode == 0) {
			for (int i = 0; i < num_disks; i++) {
                            char *disk = (char *)disk_maps[i];

                            void *indirect_block_ptr = disk + superblock->d_blocks_ptr + indirect_block_index * BLOCK_SIZE;
                            memset(indirect_block_ptr, 0, BLOCK_SIZE); // Initialize the indirect block

                            struct wfs_inode *mirror_inode = (struct wfs_inode *)(disk + superblock->i_blocks_ptr + inode->num * BLOCK_SIZE);
                            mirror_inode->blocks[IND_BLOCK] = superblock->d_blocks_ptr + indirect_block_index * BLOCK_SIZE;
                        }

		} else {
                // Mirror the allocation across all disks
                	for (int i = 0; i < num_disks; i++) {
                	    char *disk = (char *)disk_maps[i];
                	    char *data_bitmap = disk + superblock->d_bitmap_ptr;
                	    data_bitmap[indirect_block_index / 8] |= (1 << (indirect_block_index % 8));

                	    void *indirect_block_ptr = disk + superblock->d_blocks_ptr + indirect_block_index * BLOCK_SIZE;
                	    memset(indirect_block_ptr, 0, BLOCK_SIZE); // Initialize the indirect block

                	    struct wfs_inode *mirror_inode = (struct wfs_inode *)(disk + superblock->i_blocks_ptr + inode->num * BLOCK_SIZE);
                	    mirror_inode->blocks[IND_BLOCK] = superblock->d_blocks_ptr + indirect_block_index * BLOCK_SIZE;
                	}
		}
            }

            uint32_t *indirect_block = (uint32_t *)((char *)disk_maps[disk_index] + inode->blocks[IND_BLOCK]);

            if (indirect_block[indirect_offset] == 0) {
                // Allocate a new data block
                int new_block = alloc_block((char *)disk_maps[0]);
                if (new_block < 0) {
                    return -ENOSPC; // No space available
                }

		if(superblock->raid_mode == 0) {
			for (int i = 0; i < num_disks; i++) {
                            char *disk = (char *)disk_maps[i];

                            uint32_t *mirror_indirect_block = (uint32_t *)(disk + inode->blocks[IND_BLOCK]);
                            mirror_indirect_block[indirect_offset] = superblock->d_blocks_ptr + new_block * BLOCK_SIZE;
                        }
		} else {
                // Mirror the allocation across all disks
                	for (int i = 0; i < num_disks; i++) {
                	    char *disk = (char *)disk_maps[i];
                	    char *data_bitmap = disk + superblock->d_bitmap_ptr;
                	    data_bitmap[new_block / 8] |= (1 << (new_block % 8));

                	    uint32_t *mirror_indirect_block = (uint32_t *)(disk + inode->blocks[IND_BLOCK]);
                	    mirror_indirect_block[indirect_offset] = superblock->d_blocks_ptr + new_block * BLOCK_SIZE;
                	}
		}
            }

            // Write data to the block via the indirect block pointer
            size_t block_available_space = BLOCK_SIZE - block_start_offset;
            size_t bytes_to_write = (size - bytes_written < block_available_space)
                                        ? size - bytes_written
                                        : block_available_space;

	    if(superblock->raid_mode == 0) {
		char *disk = (char *)disk_maps[disk_index];
                void *block_ptr = (char *)disk + indirect_block[indirect_offset] + block_start_offset;
                memcpy(block_ptr, buf + bytes_written, bytes_to_write);
	    }
	    else {
	    	for (int i = 0; i < num_disks; i++) {
            	    char *disk = (char *)disk_maps[i];
            	    void *block_ptr = (char *)disk + indirect_block[indirect_offset] + block_start_offset;
            	    memcpy(block_ptr, buf + bytes_written, bytes_to_write);
            	}
	    }

            bytes_written += bytes_to_write;
            block_offset++;
            block_start_offset = 0; // Reset block offset for subsequent blocks
        }
    }

    // Update inode metadata on all disks
    for (int i = 0; i < num_disks; i++) {
        char *disk = (char *)disk_maps[i];
        struct wfs_inode *mirror_inode = (struct wfs_inode *)(disk + superblock->i_blocks_ptr + inode->num * BLOCK_SIZE);

        // Update size and modification time
        mirror_inode->size = (offset + size > mirror_inode->size) ? offset + size : mirror_inode->size;
        mirror_inode->mtim = time(NULL);
    }

    return bytes_written; // Return the total number of bytes written
}

static int wfs_unlink(const char *path) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];

    printf("unlink called for path: %s\n", path);

    // Step 1: Extract parent directory and filename
    char parent_path[1024], file_name[MAX_NAME];
    strncpy(parent_path, path, sizeof(parent_path));
    char *slash = strrchr(parent_path, '/');
    if (!slash || slash == parent_path) {
        strncpy(file_name, path + 1, sizeof(file_name));
        strcpy(parent_path, "/");
    } else {
        strncpy(file_name, slash + 1, sizeof(file_name));
        *slash = '\0';
    }

    // Step 2: Get the parent directory's inode
    struct wfs_inode *parent_inode = lookup_inode(parent_path, (char *)disk_maps[0]);
    if (!parent_inode || !S_ISDIR(parent_inode->mode)) {
        return -ENOENT; // Parent directory not found
    }

    // Step 3: Locate the file in the parent directory
    struct wfs_dentry *entry = NULL;
    for (int i = 0; i < D_BLOCK; i++) {
        if (parent_inode->blocks[i] == 0) break;

        struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)disk_maps[0] + parent_inode->blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (strcmp(dir_entries[j].name, file_name) == 0) {
                entry = &dir_entries[j];
                break;
            }
        }
        if (entry) break;
    }

    if (!entry) return -ENOENT; // File not found

    int file_inode_num = entry->num;

    // Step 4: Free data blocks
    for (int d = 0; d < num_disks; d++) {
        struct wfs_inode *file_inode = (struct wfs_inode *)((char *)disk_maps[d] + superblock->i_blocks_ptr + file_inode_num * BLOCK_SIZE);

        for (int i = 0; i < D_BLOCK; i++) {
            if (file_inode->blocks[i] == 0) continue;

            int block_index = (file_inode->blocks[i] - superblock->d_blocks_ptr) / BLOCK_SIZE;
            char *data_bitmap = (char *)disk_maps[d] + superblock->d_bitmap_ptr;
            data_bitmap[block_index / 8] &= ~(1 << (block_index % 8)); // Free block
            file_inode->blocks[i] = 0; // Clear block reference
        }

        // Free indirect blocks if any
        if (file_inode->blocks[IND_BLOCK] != 0) {
            uint32_t *indirect_blocks = (uint32_t *)((char *)disk_maps[d] + file_inode->blocks[IND_BLOCK]);
            for (int i = 0; i < BLOCK_SIZE / sizeof(uint32_t); i++) {
                if (indirect_blocks[i] == 0) break;

                int block_index = (indirect_blocks[i] - superblock->d_blocks_ptr) / BLOCK_SIZE;
                char *data_bitmap = (char *)disk_maps[d] + superblock->d_bitmap_ptr;
                data_bitmap[block_index / 8] &= ~(1 << (block_index % 8)); // Free block
            }

            // Free the indirect block itself
            int indirect_block_index = (file_inode->blocks[IND_BLOCK] - superblock->d_blocks_ptr) / BLOCK_SIZE;
            char *data_bitmap = (char *)disk_maps[d] + superblock->d_bitmap_ptr;
            data_bitmap[indirect_block_index / 8] &= ~(1 << (indirect_block_index % 8));
            file_inode->blocks[IND_BLOCK] = 0;
        }

        // Free inode
        char *inode_bitmap = (char *)disk_maps[d] + superblock->i_bitmap_ptr;
        inode_bitmap[file_inode_num / 8] &= ~(1 << (file_inode_num % 8));
        memset(file_inode, 0, BLOCK_SIZE); // Clear inode
    }

    // Step 5: Remove the directory entry
    for (int d = 0; d < num_disks; d++) {
        struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)disk_maps[d] + parent_inode->blocks[0]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (strcmp(dir_entries[j].name, file_name) == 0) {
                memset(&dir_entries[j], 0, sizeof(struct wfs_dentry)); // Clear directory entry
                break;
            }
        }
    }

    // Update parent directory metadata across all disks
    for (int d = 0; d < num_disks; d++) {
        struct wfs_inode *mirror_parent_inode = (struct wfs_inode *)((char *)disk_maps[d] + superblock->i_blocks_ptr + parent_inode->num * BLOCK_SIZE);
        mirror_parent_inode->size -= sizeof(struct wfs_dentry);
        mirror_parent_inode->mtim = time(NULL);
    }

    return 0; // Success
}

static int wfs_rmdir(const char *path) {

    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];

    printf("rmdir called for path: %s\n", path);

    // Step 1: Extract parent directory and target directory name
    char parent_path[1024], dir_name[MAX_NAME];
    strncpy(parent_path, path, sizeof(parent_path));
    char *slash = strrchr(parent_path, '/');
    if (!slash || slash == parent_path) {
        strncpy(dir_name, path + 1, sizeof(dir_name));
        strcpy(parent_path, "/");
    } else {
        strncpy(dir_name, slash + 1, sizeof(dir_name));
        *slash = '\0';
    }

    // Step 2: Get the parent directory's inode
    struct wfs_inode *parent_inode = lookup_inode(parent_path, (char *)disk_maps[0]);
    if (!parent_inode || !S_ISDIR(parent_inode->mode)) {
        return -ENOENT; // Parent directory not found
    }

    // Step 3: Locate the target directory in the parent directory
    struct wfs_dentry *entry = NULL;
    for (int i = 0; i < D_BLOCK; i++) {
        if (parent_inode->blocks[i] == 0) break;

        struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)disk_maps[0] + parent_inode->blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (strcmp(dir_entries[j].name, dir_name) == 0) {
                entry = &dir_entries[j];
                break;
            }
        }
        if (entry) break;
    }

    if (!entry) return -ENOENT; // Directory not found

    int dir_inode_num = entry->num;
    struct wfs_inode *dir_inode = (struct wfs_inode *)((char *)disk_maps[0] + superblock->i_blocks_ptr + dir_inode_num * BLOCK_SIZE);

    // Step 4: Check that the directory is empty
    for (int i = 0; i < D_BLOCK; i++) {
        if (dir_inode->blocks[i] == 0) continue;

        struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)disk_maps[0] + dir_inode->blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (dir_entries[j].num != 0 && strcmp(dir_entries[j].name, ".") != 0 && strcmp(dir_entries[j].name, "..") != 0) {
                return -ENOTEMPTY; // Directory is not empty
            }
        }
    }

    // Step 5: Free directory blocks and inode
    for (int d = 0; d < num_disks; d++) {
        struct wfs_inode *mirror_dir_inode = (struct wfs_inode *)((char *)disk_maps[d] + superblock->i_blocks_ptr + dir_inode_num * BLOCK_SIZE);

        for (int i = 0; i < D_BLOCK; i++) {
            if (mirror_dir_inode->blocks[i] == 0) continue;

            int block_index = (mirror_dir_inode->blocks[i] - superblock->d_blocks_ptr) / BLOCK_SIZE;
            if (superblock->raid_mode == 0 && block_index % num_disks != d) continue; // Skip non-relevant disks for RAID 0

            char *data_bitmap = (char *)disk_maps[d] + superblock->d_bitmap_ptr;
            data_bitmap[block_index / 8] &= ~(1 << (block_index % 8));
            mirror_dir_inode->blocks[i] = 0; // Clear block reference
        }

        // Free inode
        char *inode_bitmap = (char *)disk_maps[d] + superblock->i_bitmap_ptr;
        inode_bitmap[dir_inode_num / 8] &= ~(1 << (dir_inode_num % 8));
        memset(mirror_dir_inode, 0, BLOCK_SIZE);
    }

    // Step 6: Remove the directory entry from the parent directory
    for (int d = 0; d < num_disks; d++) {
        struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)disk_maps[d] + parent_inode->blocks[0]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (strcmp(dir_entries[j].name, dir_name) == 0) {
                memset(&dir_entries[j], 0, sizeof(struct wfs_dentry)); // Clear directory entry
                break;
            }
        }
    }

    // Step 7: Update parent directory metadata across all disks
    for (int d = 0; d < num_disks; d++) {
        struct wfs_inode *mirror_parent_inode = (struct wfs_inode *)((char *)disk_maps[d] + superblock->i_blocks_ptr + parent_inode->num * BLOCK_SIZE);
        mirror_parent_inode->size -= sizeof(struct wfs_dentry);
        mirror_parent_inode->mtim = time(NULL);
    }

    return 0; // Success
}


















int main(int argc, char *argv[]) {

    num_disks = 0;
    int fd;

    if (argc < 3) {
        return -1;
    }

    // Identify the number of disks (all arguments before FUSE options or mount point)
    while (num_disks + 1 < argc && argv[num_disks + 1][0] != '-') {
        num_disks++;
    }

    // printf("num_disks is %i\n", num_disks);

    for(int i=0; i<num_disks; i++) {
        fd = open(argv[i + 1], O_RDWR);
        // printf("fds[i] == %i\n", fd);


        struct stat temp;
        int checker = fstat(fd, &temp);
        
        if(checker == 0) {
            disk_maps[i] = mmap(NULL, temp.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (disk_maps[i] == MAP_FAILED) {
                // printf("this failed SDFJDAKLSFJDASLKFJ\n");
                close(fd);
                exit(-1);
            }
        }
        else {
            // printf(" this si whats wrong fds[%i] == %i\n", i, fd);
            // printf("Error getting file stats this is our own error");
            close(fd);
            exit(-1);
        }

        close(fd);
    }

    int fuse_argc = argc - num_disks;
    char **fuse_argv = malloc(fuse_argc * sizeof(char *));
    if (!fuse_argv) {
        return -1;
    }

    fuse_argv[0] = argv[0];
    for (int i = num_disks + 1; i < argc; i++) {
        fuse_argv[i - num_disks] = argv[i];
    }

    // printf("FUSE argc: %d\n", fuse_argc);
    // printf("FUSE argv:\n");
    // for (int i = 0; i < fuse_argc; i++) {
    //     printf("  argv[%d]: %s\n", i, fuse_argv[i]);
    // }

    // for(int i=0; i<argc; i++) {
    //     free(fuse_argv[i]);
    // }
    // free(fuse_argv);

    return fuse_main(fuse_argc, fuse_argv, &ops, NULL);;
}