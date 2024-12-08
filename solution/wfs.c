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
#include <libgen.h>


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






// zeros out given block
void clear_block(char *block_address) {

    // don't even need to set it to anything
    char zeroed_block[BLOCK_SIZE];
    memset(zeroed_block, 0, BLOCK_SIZE);

    memcpy(block_address, zeroed_block, BLOCK_SIZE);
}

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

                    // Zero out the new block
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




int setup_entry(int i, struct wfs_dentry *entries, char *new_name, int new_inode_index) {
    strncpy(entries[i].name, new_name, 28);
    entries[i].num = new_inode_index;

    return 0;
}

int wfs_mkdir_helper(const char *path, mode_t mode, char *disk) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];

    // Copy path to mutable buffers for basename() and dirname()
    char temp_dirname[28];
    strncpy(temp_dirname, path, sizeof(temp_dirname));
    char *parent = dirname(temp_dirname);

    struct wfs_inode *parent_i = lookup_inode(parent, disk);

    int new_inode_index = alloc_inode(disk);
    if (new_inode_index < 0) {
        return new_inode_index;
    }

    if (!parent_i) {
        return -ENOENT;
    }

    char temp_basename[28];
    strncpy(temp_basename, path, sizeof(temp_basename));
    char *new_name = basename(temp_basename);

    if (wfs_mkdir_helper_raid_mode(mode, new_inode_index, disk, parent) != 0) {
        return -1;
    } 
    
    
    else {
        struct wfs_dentry *entries;
        for (int i = 0; i < D_BLOCK; i++) {

            if (parent_i->blocks[i] == 0) {

                int block_index = alloc_block(disk);
                if (block_index < 0) {
                    return ENOSPC;
                }

                int block_num;
                switch (superblock->raid_mode) {
                    case 0:
                        for (int d = 0; d < num_disks; d++) {

                            block_num = block_index / num_disks;
                            struct wfs_inode *x = lookup_inode(parent, (char *)disk_maps[0]);

                            x->blocks[i] = superblock->d_blocks_ptr + (block_num * BLOCK_SIZE);
                        }
                        break;

                    case 1:

                        // logic maybe casing bug? idk
                        parent_i->blocks[i] = (block_index * BLOCK_SIZE) + superblock-> d_blocks_ptr;
                        break;

                    default:

                        // has to be raid mode 1 or 0
                        return -EINVAL;
                }
            }

            entries = (struct wfs_dentry *)(disk + parent_i-> blocks[i]);
            for (int j = 0; j < BLOCK_SIZE; j+= sizeof(struct wfs_dentry)) {
                
                if(entries[(j/sizeof(struct wfs_dentry))].num != 0) {
                    continue;
                }

                else {

                    int rc = setup_entry((j/sizeof(struct wfs_dentry)), entries, new_name, new_inode_index);
                    parent_i->size += sizeof(struct wfs_dentry);

                    return rc;
                }
            }
        }

        // could not find anywhere to put it, no space
        return -ENOSPC;

    }
}



static int wfs_mkdir(const char *path, mode_t mode) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];

    switch (superblock->raid_mode) {
        case 0:
            wfs_mkdir_helper(path, mode, (char *)disk_maps[0]);
            break;

        case 1:
            for (int i = 0; i < num_disks; i++) {
                wfs_mkdir_helper(path, mode, (char *)disk_maps[i]);
            }
            break;

        default:
            return -EINVAL;
    }

    return 0;
}


int set_new_mknod(mode_t mode, struct wfs_inode *new_inode, int new_inode_index) {
    memset(new_inode, 0, BLOCK_SIZE);
    new_inode->num = new_inode_index;
    new_inode->mode = S_IFREG | mode; // Set directory mode
    new_inode->nlinks = 1;           // "." and parent's link
    new_inode->size = 0;             // Empty directory initially
    new_inode->atim = new_inode->mtim = new_inode->ctim = time(NULL);
    new_inode->uid = getuid();
    new_inode->gid = getgid();

    return 0;
}












static int wfs_mknod_helper(const char *path, mode_t mode, char *disk) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];

    char parent_path[1024];
    char new_name[28];
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

            if(set_new_mknod(mode, new_inode, new_inode_index) != 0) {
                return -1;
            }

        }

    } else {
        char *inode_table = disk + superblock->i_blocks_ptr;
        struct wfs_inode *new_inode = (struct wfs_inode *)(inode_table + (new_inode_index * BLOCK_SIZE));
        
        if(set_new_mknod(mode, new_inode, new_inode_index) != 0) {
            return -1;
        }

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
    (void)fi;

    char* superblock_char = (char *)disk_maps[0];
    struct wfs_inode *inode;
    struct wfs_dentry *entries;

    if (!(inode = lookup_inode(path, superblock_char))) {
        return -ENOENT;
    }

    if (!S_ISDIR(inode->mode)) {
        return -ENOTDIR;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);


    int i = 0;
    while (i < D_BLOCK && inode->blocks[i] != 0) {
        entries = (struct wfs_dentry *) (superblock_char + inode-> blocks[i]);

        for (int j = 0; j < BLOCK_SIZE; j+= sizeof(struct wfs_dentry)) {
            if (entries[(j/ sizeof(struct wfs_dentry))].num == 0) {
                continue;
            }
            else {
                filler(buf, entries[(j / sizeof(struct wfs_dentry))].name, NULL, 0);
            }
        }

        i++;
    }

    return 0;
}




void *get_data_block(struct wfs_inode *inode, size_t offset, char *disk_map) {

    bool direct = (offset < D_BLOCK);
    size_t offset_for_indirect;
    char* rc;
    uint32_t *indirect_block;

    if (!direct) {
        offset_for_indirect = (offset - D_BLOCK);

        if (inode->blocks[IND_BLOCK] == 0) {
            return NULL;
        }

        indirect_block = (uint32_t *)(disk_map + inode->blocks[IND_BLOCK]);
        if (indirect_block[offset_for_indirect] == 0) {
            rc = NULL;
        }
        else {
            rc = disk_map + indirect_block[offset_for_indirect];
        }

    } else if (direct) {

        if(inode->blocks[offset] != 0) {
            rc =  disk_map + inode->blocks[offset];
        }

        else {
            rc = NULL;
        }
    }

    return rc;

}



static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct wfs_inode *inode;
    char *disk_map = (char *)disk_maps[0];
    size_t read = 0;
    size_t block_offset = 0;;
    size_t block_start_offset = 0;
    size_t bytes_to_read = 0;
    bool overflow = size < read;
    bool side;
    char* buf_read;
    char* db_off;


    if (!(inode = lookup_inode(path, disk_map))) {
        return -ENOENT;
    }
    else if (!S_ISREG(inode->mode)) {
        return -EISDIR;
    }

    block_offset = offset / BLOCK_SIZE;
    block_start_offset = offset % BLOCK_SIZE;

    while (!overflow) {
        char* data_block = (char *)get_data_block(inode, block_offset, disk_map);
        side = (size - read < (BLOCK_SIZE - block_start_offset));

        if (!data_block) {
            return read;
        }

        if(side) {
            bytes_to_read = size - read;
        }
        else if (!side) {
            bytes_to_read = (BLOCK_SIZE - block_start_offset);
        }


        buf_read = buf + read;
        db_off = data_block + block_start_offset;
        memcpy(buf_read, db_off, bytes_to_read);
        
        read += bytes_to_read;
        overflow = size < read;

        block_start_offset = 0;
        block_offset++;
        
    }

    return read;
}




int wfs_write_helper1(struct wfs_inode *inode, off_t block_offset, int new, int vbn) {

    char *data_bitmap;
    struct wfs_sb *superblock = (struct wfs_sb *) disk_maps[0];
    struct wfs_inode *other;
    char *disk;

    for (int i = 0; i < num_disks; i++) {

        disk = (char *) disk_maps[i];
        other = (struct wfs_inode *)(disk + (inode->num * BLOCK_SIZE) + superblock->i_blocks_ptr);

        switch (superblock-> raid_mode) {
            case 0: 
                other->blocks[vbn] = superblock->d_blocks_ptr + (new * BLOCK_SIZE);
                break;

            case 1:
            
            default:
                data_bitmap = disk + superblock->d_bitmap_ptr;
                data_bitmap[new / 8] |= (1 << (new % 8));
                other->blocks[block_offset] = superblock->d_blocks_ptr + (new * BLOCK_SIZE);
                break;
        }
    }

    return 0;
}

int wfs_write_helper2(struct wfs_inode *inode, int vbn, const char *buf, size_t bytes_written, size_t to_write, size_t block_offset, size_t block_start_offset) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    int cap;

    if(superblock -> raid_mode == 1) {
        cap = num_disks;
    }
    else if (superblock -> raid_mode == 0) {
        cap = 1;
    }
    else {
        return -EINVAL;
    }

    for (int i = 0; i < cap; i++) {

    char *disk = (char *) disk_maps[i];
    void *block_ptr;

    switch (superblock-> raid_mode) {
        case 0:
            if (i != 0) {
                continue;
            } 
            else {
                block_ptr = (char *) disk + inode->blocks[vbn] + block_start_offset;
                memcpy(block_ptr, buf + bytes_written, to_write);
            }
            break;

        case 1:

        default:
            block_ptr = (char *)disk + inode->blocks[block_offset] + block_start_offset;
            memcpy(block_ptr, buf + bytes_written, to_write);
            break;
    }
}

    return 0;
}



int wfs_write_helper3(struct wfs_inode *inode, int indirect_block_index) {
    
    char *data_bitmap;
    struct wfs_sb *superblock = (struct wfs_sb *) disk_maps[0];
    int byte = 8;
    int block_num = indirect_block_index / byte;
    int block_off = indirect_block_index % byte;

    for (int i = 0; i < num_disks; i++) {

        char *disk = (char *) disk_maps[i];
        struct wfs_inode *other = (struct wfs_inode *)(superblock->i_blocks_ptr +disk + (inode->num * BLOCK_SIZE));
        void *indirect_block_ptr = disk + superblock->d_blocks_ptr + indirect_block_index * BLOCK_SIZE;
        memset(indirect_block_ptr, 0, BLOCK_SIZE);

        switch (superblock->raid_mode) {

            case 0:
                other->blocks[IND_BLOCK] = superblock->d_blocks_ptr + (indirect_block_index * BLOCK_SIZE);
                break;

            case 1:
            
            default:
                data_bitmap =superblock-> d_bitmap_ptr + disk;

                int bits = (1 << block_off);
                data_bitmap[block_num] = data_bitmap[block_num] | bits;

                other->blocks[IND_BLOCK] = superblock->d_blocks_ptr + (indirect_block_index * BLOCK_SIZE);
                break;
        }
    }

    return 0;
}



int wfs_write_helper4(struct wfs_inode *inode, size_t indirect_offset, int new) {
    char *data_bitmap;
    struct wfs_sb *superblock = (struct wfs_sb *) disk_maps[0];
    char *disk;
    uint32_t *other_ind_block;
    int bytes = 8;
    int block_off = new % bytes;
    int block_num = new / bytes;


    for (int i = 0; i < num_disks; i++) {

        disk = (char *) disk_maps[i];
        other_ind_block = (uint32_t *)(disk + inode->blocks[IND_BLOCK]);

        switch (superblock-> raid_mode) {
            case 0:
                other_ind_block[indirect_offset] = superblock->d_blocks_ptr + (new * BLOCK_SIZE);
                break;

            case 1:
            default:
                data_bitmap = disk + superblock->d_bitmap_ptr;

                int bits = 1 << (block_off);
                data_bitmap[block_num] = data_bitmap[block_num] | bits;

                other_ind_block[indirect_offset] = superblock->d_blocks_ptr + (new * BLOCK_SIZE);
                break;
        }
    }

    return 0;
}


int wfs_write_helper5(int disk_index, size_t indirect_offset, size_t block_start_offset, const char *buf, size_t bytes_written, size_t to_write, uint32_t *indirect_block) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];

    int cap;
    char *disk;
    void *block_ptr;

    if(superblock -> raid_mode == 1) {
        cap = num_disks;
    }
    else if (superblock -> raid_mode == 0) {
        cap = 1;
    }
    else {
        return -EINVAL;
    }

    for (int i = 0; i < cap; i++) {

        if(superblock-> raid_mode == 0) {
            disk = (char *) disk_maps[disk_index];
        } 
        else if (superblock-> raid_mode == 1) {
            disk = (char *) disk_maps[i];
        } 
        else {
            return -EINVAL;
        }

        block_ptr = disk + block_start_offset + indirect_block[indirect_offset];

        switch (superblock->raid_mode) {
            case 0:
                if(i !=0) {
                    continue;
                }
                if (i == 0) {
                    memcpy(block_ptr, buf + bytes_written, to_write);
                }
                break;

            case 1:
            default:
                memcpy(block_ptr, buf + bytes_written, to_write);
                break;
        }
    }

    return 0;

}


// void wfs_write_reset_helper(size_t bytes_written, size_t to_write, size_t block_offset,) {
//     bytes_written += to_write;
//     block_offset++;
//     block_start_offset = 0;
// }


static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    struct wfs_inode *inode = lookup_inode(path, (char *)disk_maps[0]);
    // char *data_bitmap;

    if (!inode) {
        return -ENOENT;
    }

    if (!S_ISREG(inode->mode)) {
        return -EISDIR;
    }

    size_t bytes_written = 0;
    size_t block_offset = offset / BLOCK_SIZE;
    size_t block_start_offset = offset % BLOCK_SIZE;
    int indirect_block_index;
    size_t block_available_space;
    size_t to_write;
    uint32_t *indirect_block;
    size_t indirect_offset;

    while (bytes_written < size) {
        int disk_index = 0, vbn = block_offset;

        switch (superblock->raid_mode) {
            case 0:
                disk_index = block_offset % num_disks;
                vbn = block_offset / num_disks;
                break;
            case 1: 
            default:
                disk_index = 0;
                vbn = block_offset;
                break;
        }

        if (block_offset < D_BLOCK) {
            if (inode->blocks[block_offset] == 0) {
                int new = alloc_block((char *)disk_maps[disk_index]);

                if (new >= 0) {
                    wfs_write_helper1(inode, block_offset, new, vbn);
                } else return -ENOSPC;
            }

            size_t block_available_space = BLOCK_SIZE - block_start_offset;

            size_t to_write;
            bool checker = size - bytes_written < block_available_space;

             if(checker) {
                to_write = size - bytes_written;
             } else if (!checker) {
                to_write = block_available_space;
             }

            wfs_write_helper2(inode, vbn, buf, bytes_written, to_write, block_offset, block_start_offset);

            bytes_written += to_write;
            block_offset++;
            block_start_offset = 0;

        } else {
            indirect_offset = block_offset - D_BLOCK;

            if (inode->blocks[IND_BLOCK] != 0) {
                continue;
            }
            else {

                indirect_block_index = alloc_block((char *) disk_maps[disk_index]);
                if (indirect_block_index < 0) {
                    return -ENOSPC;
                }

                wfs_write_helper3(inode, indirect_block_index);

            }

            indirect_block = (uint32_t *)((char *)disk_maps[disk_index] + inode->blocks[IND_BLOCK]);

            if (indirect_block[indirect_offset] == 0) {
                int new = alloc_block((char *) disk_maps[0]);
                if (new < 0) {
                    return -ENOSPC;
                }

                wfs_write_helper4(inode, indirect_offset, new);
            }

            block_available_space = BLOCK_SIZE - block_start_offset;

            int check1 = (size - bytes_written < block_available_space);
            if(!check1) {
                to_write = block_available_space;
            } else if (check1) {
                to_write = size - bytes_written
            }

            wfs_write_helper5(disk_index, indirect_offset, block_start_offset, buf, bytes_written, to_write, indirect_block);

            bytes_written += to_write;
            block_offset++;
            block_start_offset = 0;
        }
    }

    for (int i = 0; i < num_disks; i++) {
        char *disk = (char *) disk_maps[i];

        struct wfs_inode *other = (struct wfs_inode *)(disk + superblock->i_blocks_ptr + inode->num * BLOCK_SIZE);

        if(offset + size > other->size) {
            other->size = offset + size;
        }

        other->mtim = time(NULL);
    }

    return bytes_written;
}


/*

    UNLINK HELPERS

*/

// Step 1: Extract parent directory and filename
int extract_parent_and_name(const char *path, char *parent_path, char *file_name) {
    strncpy(parent_path, path, 1024);
    char *slash = strrchr(parent_path, '/');
    if (!slash || slash == parent_path) {
        strncpy(file_name, path + 1, MAX_NAME);
        strcpy(parent_path, "/");
    } else {
        strncpy(file_name, slash + 1, MAX_NAME);
        *slash = '\0';
    }
    return 0;
}

// Step 2: Get the parent directory's inode
struct wfs_inode *get_parent_inode(const char *parent_path) {
    struct wfs_inode *parent_inode = lookup_inode(parent_path, (char *)disk_maps[0]);
    if (!parent_inode || !S_ISDIR(parent_inode->mode)) {
        return NULL;
    }
    return parent_inode;
}

// Step 3: Locate the file in the parent directory
struct wfs_dentry *find_file_entry(struct wfs_inode *parent_inode, const char *file_name) {
    // struct wfs_dentry *entry = NULL;
    for (int i = 0; i < D_BLOCK; i++) {
        if (parent_inode->blocks[i] == 0) break;

        struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)disk_maps[0] + parent_inode->blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (strcmp(dir_entries[j].name, file_name) == 0) {
                return &dir_entries[j];
            }
        }
    }
    return NULL;
}

// Step 4: Free data blocks
int free_file_blocks(int file_inode_num) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    for (int d = 0; d < num_disks; d++) {
        struct wfs_inode *file_inode = (struct wfs_inode *)((char *)disk_maps[d] + superblock->i_blocks_ptr + file_inode_num * BLOCK_SIZE);

        for (int i = 0; i < D_BLOCK; i++) {
            if (file_inode->blocks[i] == 0) continue;

            int block_index = (file_inode->blocks[i] - superblock->d_blocks_ptr) / BLOCK_SIZE;
            char *data_bitmap = (char *)disk_maps[d] + superblock->d_bitmap_ptr;
            data_bitmap[block_index / 8] &= ~(1 << (block_index % 8));
            file_inode->blocks[i] = 0;
        }

        if (file_inode->blocks[IND_BLOCK] != 0) {
            uint32_t *indirect_blocks = (uint32_t *)((char *)disk_maps[d] + file_inode->blocks[IND_BLOCK]);
            for (int i = 0; i < BLOCK_SIZE / sizeof(uint32_t); i++) {
                if (indirect_blocks[i] == 0) break;

                int block_index = (indirect_blocks[i] - superblock->d_blocks_ptr) / BLOCK_SIZE;
                char *data_bitmap = (char *)disk_maps[d] + superblock->d_bitmap_ptr;
                data_bitmap[block_index / 8] &= ~(1 << (block_index % 8));
            }

            int indirect_block_index = (file_inode->blocks[IND_BLOCK] - superblock->d_blocks_ptr) / BLOCK_SIZE;
            char *data_bitmap = (char *)disk_maps[d] + superblock->d_bitmap_ptr;
            data_bitmap[indirect_block_index / 8] &= ~(1 << (indirect_block_index % 8));
            file_inode->blocks[IND_BLOCK] = 0;
        }

        char *inode_bitmap = (char *)disk_maps[d] + superblock->i_bitmap_ptr;
        inode_bitmap[file_inode_num / 8] &= ~(1 << (file_inode_num % 8));
        memset(file_inode, 0, BLOCK_SIZE);
    }
    return 0;
}

// Step 5: Remove the directory entry
int remove_directory_entry(struct wfs_inode *parent_inode, const char *file_name) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    for (int d = 0; d < num_disks; d++) {
        struct wfs_dentry *dir_entries = (struct wfs_dentry *)((char *)disk_maps[d] + parent_inode->blocks[0]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (strcmp(dir_entries[j].name, file_name) == 0) {
                memset(&dir_entries[j], 0, sizeof(struct wfs_dentry));
                break;
            }
        }
    }

    for (int d = 0; d < num_disks; d++) {
        struct wfs_inode *mirror_parent_inode = (struct wfs_inode *)((char *)disk_maps[d] + superblock->i_blocks_ptr + parent_inode->num * BLOCK_SIZE);
        mirror_parent_inode->size -= sizeof(struct wfs_dentry);
        mirror_parent_inode->mtim = time(NULL);
    }
    return 0;
}

// Main unlink function
static int wfs_unlink(const char *path) {
    // struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    char parent_path[1024], file_name[MAX_NAME];

    if (extract_parent_and_name(path, parent_path, file_name) < 0) {
        return -EINVAL;
    }

    struct wfs_inode *parent_inode = get_parent_inode(parent_path);
    if (!parent_inode) {
        return -ENOENT;
    }

    struct wfs_dentry *entry = find_file_entry(parent_inode, file_name);
    if (!entry) {
        return -ENOENT;
    }

    int file_inode_num = entry->num;
    if (free_file_blocks(file_inode_num) < 0) {
        return -EIO;
    }

    if (remove_directory_entry(parent_inode, file_name) < 0) {
        return -EIO;
    }

    return 0;
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