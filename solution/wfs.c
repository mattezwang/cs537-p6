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


struct wfs_inode *lookup_inode_recursive(const char *fp, char *disk, struct wfs_inode *current_inode) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    char* disk_map = (char *)disk_maps[0];

    if (strcmp(fp, "/") == 0 || *fp == '\0') {
        return current_inode;
    }
    const char *first_part = strchr(fp, '/');
    size_t first_part_length;

    if(first_part == NULL) {
        first_part_length = strlen(fp);
    } else {
        first_part_length = (size_t)(first_part) - (size_t)(fp);
    }

    char token[28];
    strncpy(token, fp, first_part_length);
    token[first_part_length] = '\0';


    struct wfs_dentry *entry;
    int i = 0;
    while (i < D_BLOCK && current_inode -> blocks[i] > 0) {

        entry = (struct wfs_dentry *)(disk_map + current_inode->blocks[i]);
        int block_n;
        int checker;
        for (int j = 0; j < BLOCK_SIZE; j+= sizeof(struct wfs_dentry)) {
            block_n = j/sizeof(struct wfs_dentry);
            checker = strcmp(entry[block_n].name, token);

            if(checker != 0) {
                continue;
            }
            else {
                struct wfs_inode *next_inode = (struct wfs_inode *)(disk + superblock->i_blocks_ptr + (entry[block_n].num * BLOCK_SIZE));
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
    return NULL;
}


struct wfs_inode *lookup_inode(const char *fp, char *disk) {
    
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    struct wfs_inode *root_inode = (struct wfs_inode *)(disk + superblock->i_blocks_ptr);

    const char *param = fp;
    if(fp[0] == '/') {
        param = fp+1;
    }

    return lookup_inode_recursive(param, disk, root_inode);
}


int find_free_inode(char *bitmap_ptr, int num_inodes) {
    int byte = 8;
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


int alloc_inode(char *disk) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    char *inode_map_ptr = disk + superblock->i_bitmap_ptr;;
    int inode;
    int byte = 8;
    int available;

    switch (superblock -> raid_mode) {
        case 0: {
            for (int i = 0; i < superblock->num_inodes; i++) {
                int bitmap_byte = i / byte;
                int bitmap_bit = i % byte;

                available = 1;
                for (int j = 0; j < num_disks; j++) {
                   inode_map_ptr = (char *)disk_maps[j] + superblock -> i_bitmap_ptr;
                    if (inode_map_ptr[bitmap_byte] & (1 << bitmap_bit)) { 
                        available = 0;
                        break;
                    }
                }

                if (available) {
                    
                    for (int j = 0; j < num_disks; j++) {
                       inode_map_ptr = (char *)disk_maps[j] + superblock->i_bitmap_ptr;
                       inode_map_ptr[bitmap_byte] |= (1 << bitmap_bit);
                    }
                    return i;
                }
            }

            break;
        }

        default: {

            inode = find_free_inode(inode_map_ptr, superblock->num_inodes);

            if(inode < 0) {
                break;
            }
            else {
               inode_map_ptr[inode / byte] |= (1 << (inode % byte));
            }
            break;
        }
    }
    
    if (inode < 0) {
        inode = -ENOSPC;
    }
    return inode;
}


void clear_block(char *block_address) {
    char zeroed_block[BLOCK_SIZE];
    memset(zeroed_block, 0, BLOCK_SIZE);
    memcpy(block_address, zeroed_block, BLOCK_SIZE);
}


int alloc_block(char *disk) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    char *data_map_char = disk + superblock->d_bitmap_ptr;
    int byte = 8;

    switch (superblock->raid_mode) {

        case 0: {
            for (int i = 0; i < superblock->num_data_blocks; i++) {
                int current_disk = i % num_disks;
                uint8_t *data_map = (uint8_t *)disk_maps[current_disk] + superblock -> d_bitmap_ptr;
                int block_n_in_disk = i / num_disks;

                int bitmap_byte = block_n_in_disk / byte;
                int bitmap_bit = block_n_in_disk % byte;


                if((data_map[bitmap_byte] & (1 << bitmap_bit))) {
                    continue;
                }

                else {
                    data_map[bitmap_byte] |= (1 << bitmap_bit);
                    char *block_address = (char *)disk_maps[current_disk] + superblock-> d_blocks_ptr + (block_n_in_disk * BLOCK_SIZE);
                    clear_block(block_address);
                    return i;
                }
            }
            break;
        }
        
        case 1:
        default: {
            for (int i = 0; i < superblock ->num_data_blocks; i++) {
                if((data_map_char[i / byte] & (1 << (i % byte)))) {
                    continue;
                }

                else {
                    data_map_char[i / byte] |= (1 << (i % byte));
                    char *block_address = disk + (i * BLOCK_SIZE) + superblock -> d_blocks_ptr;
                    clear_block(block_address);
                    return i;
                }
            }

            break;
        }
    }

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
        stbuf->st_mode = inode->mode;    
        stbuf->st_nlink = inode->nlinks; 
        stbuf->st_uid = inode->uid;      
        stbuf->st_gid = inode->gid;      
        stbuf->st_size = inode->size;    
        stbuf->st_atime = inode->atim;  
        stbuf->st_mtime = inode->mtim;   
        stbuf->st_ctime = inode->ctim;   
    }

    return rc;
}


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


int wfs_mkdir_helper_raid_mode(mode_t mode, int new_inode_index, char *disk) {
    struct wfs_sb *superblock = (struct wfs_sb *) disk_maps[0];
    char *inode_map;
    struct wfs_inode *inode;

    switch (superblock -> raid_mode) {
        case 0: {

            for (int i = 0; i < num_disks; i++) {

                inode_map = ((char *) disk_maps[i] + superblock -> i_blocks_ptr);
                inode = (struct wfs_inode *)((new_inode_index * BLOCK_SIZE) + inode_map);

                if (set_new_inode(mode, inode, new_inode_index) != 0) {
                    return -1;
                }
                else {
                    continue;
                }
            }

            break;
        }

        case 1: {
            inode_map = (disk + superblock->i_blocks_ptr);
            inode = (struct wfs_inode *)(inode_map + (new_inode_index * BLOCK_SIZE));

            if (set_new_inode(mode, inode, new_inode_index) != 0) {
                return -1;
            }

            else {
                break;
            }
        }

        default: {
            return -EINVAL;
        }
    }

    return 0;
}


int setup_entry(int i, struct wfs_dentry *entries, char *new_name, int new_inode_index) {
    strncpy(entries[i].name, new_name, 28);
    entries[i].num = new_inode_index;
    return 0;
}


int wfs_mkdir_helper(const char *path, mode_t mode, char *disk) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];

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

    if (wfs_mkdir_helper_raid_mode(mode, new_inode_index, disk) != 0) {
        return -1;
    } 
    
    
    else {
        struct wfs_dentry *entries;
        for (int i = 0; i < D_BLOCK; i++) {

            if (parent_i->blocks[i] == 0) {

                int block_location = alloc_block(disk);
                if (block_location < 0) {
                    return ENOSPC;
                }

                int block_n;
                switch (superblock->raid_mode) {
                    case 0:
                        for (int k = 0; k < num_disks; k++) {

                            block_n = block_location / num_disks;
                            struct wfs_inode *x = lookup_inode(parent, (char *)disk_maps[0]);

                            x->blocks[i] = superblock->d_blocks_ptr + (block_n * BLOCK_SIZE);
                        }
                        break;

                    case 1:
                        parent_i->blocks[i] = (block_location * BLOCK_SIZE) + superblock-> d_blocks_ptr;
                        break;

                    default:
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
    new_inode->mode = S_IFREG | mode;
    new_inode->nlinks = 1;
    new_inode->size = 0; 
    new_inode->atim = new_inode->mtim = new_inode->ctim = time(NULL);
    new_inode->uid = getuid();
    new_inode->gid = getgid();
    return 0;
}


static int wfs_mknod_helper(const char *path, mode_t mode, char *disk) { 
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    char temp_dirname[28];
    char parent_filepath[1024];
    strncpy(parent_filepath, path, sizeof(parent_filepath));

    char *slash = strrchr(parent_filepath, '/');
    if (!slash || slash == parent_filepath) {
        strncpy(temp_dirname, path + 1, sizeof(temp_dirname));
        strcpy(parent_filepath, "/");
    } else {
        strncpy(temp_dirname, slash + 1, sizeof(temp_dirname));
        *slash = '\0';
    }

    int index = alloc_inode(disk);
    if (index < 0) {
        return -ENOSPC; 
    }

    struct wfs_inode *inode_parent = lookup_inode(parent_filepath, disk);
    if (!inode_parent) {
        return -ENOENT; 
    }

    if(superblock->raid_mode == 1) {
        char *inode_map = disk + superblock->i_blocks_ptr;
        struct wfs_inode *new_inode = (struct wfs_inode *)(inode_map + (index * BLOCK_SIZE));
        if(set_new_mknod(mode, new_inode, index) != 0) {
            return -1;
        }
    } else {

        for(int i = 0; i < num_disks; i++) {
            char *inode_map = ((char *)disk_maps[i] + superblock->i_blocks_ptr);
            struct wfs_inode *new_inode =(struct wfs_inode *) (inode_map + (index * BLOCK_SIZE));
            if(set_new_mknod(mode, new_inode, index) != 0) {
                return -1;
            }

        }

    }
    for (int i = 0; i < D_BLOCK; i++) {
        if (inode_parent->blocks[i] == 0) {
            int b_index = alloc_block(disk);
            if (b_index < 0) {
                return -ENOSPC;
            }
            inode_parent->blocks[i] = superblock->d_blocks_ptr + b_index * BLOCK_SIZE;
        }

        struct wfs_dentry *dir_elements = (struct wfs_dentry *)(disk + inode_parent->blocks[i]);
        int entry_found = 0;
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {
            if (dir_elements[j].num == 0) {
                strncpy(dir_elements[j].name, temp_dirname, MAX_NAME);
                dir_elements[j].num = index;
                inode_parent->size += sizeof(struct wfs_dentry);
                entry_found = 1;
                break;
            }
        }
        if (entry_found) {
            return 0;
        }
    }
    return -ENOSPC;

}


static int wfs_mknod(const char *path, mode_t mode, dev_t rdev) {
    (void)rdev;
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    int status = 0;
    if(superblock->raid_mode == 1){
        for (int x = 0; x < num_disks; x++) {
        status = wfs_mknod_helper(path, mode, (char *)disk_maps[x]);
        if (status < 0) {
            for (int y = 0; y <= x; y++) {
                char *disk = (char *)disk_maps[y];
                char *inode_map = disk + superblock->i_bitmap_ptr;
                int inode_num = alloc_inode(disk);
                if (inode_num >= 0) {
                    inode_map[inode_num / 8] ^= (1 << (inode_num % 8)) & inode_map[inode_num / 8];
                }
            }
            return status; 
        }
        }
    }else{
        status = wfs_mknod_helper(path, mode, (char *)disk_maps[0]);
        return status;
    }
    return status;

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
    size_t start_offset = 0;
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
    start_offset = offset % BLOCK_SIZE;

    while (!overflow) {
        char* data_block = (char *)get_data_block(inode, block_offset, disk_map);
        side = (size - read < (BLOCK_SIZE - start_offset));

        if (!data_block) {
            return read;
        }

        if(side) {
            bytes_to_read = size - read;
        }
        else if (!side) {
            bytes_to_read = (BLOCK_SIZE - start_offset);
        }


        buf_read = buf + read;
        db_off = data_block + start_offset;
        memcpy(buf_read, db_off, bytes_to_read);
        
        read += bytes_to_read;
        overflow = size < read;

        start_offset = 0;
        block_offset++;
        
    }

    return read;
}


int wfs_write_helper1(struct wfs_inode *inode, off_t block_offset, int new, int vbn) {

    char *data_map;
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
                data_map = disk + superblock->d_bitmap_ptr;
                data_map[new / 8] |= (1 << (new % 8));
                other->blocks[block_offset] = superblock->d_blocks_ptr + (new * BLOCK_SIZE);
                break;
        }
    }

    return 0;
}


int wfs_write_helper2(struct wfs_inode *inode, int vbn, const char *buf, size_t bytes_written, size_t to_write, size_t block_offset, size_t start_offset) {
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
                block_ptr = (char *) disk + inode->blocks[vbn] + start_offset;
                memcpy(block_ptr, buf + bytes_written, to_write);
            }
            break;

        case 1:

        default:
            block_ptr = (char *)disk + inode->blocks[block_offset] + start_offset;
            memcpy(block_ptr, buf + bytes_written, to_write);
            break;
    }
}

    return 0;
}


int wfs_write_helper3(struct wfs_inode *inode, int indirect_block_location) {
    
    char *data_map;
    struct wfs_sb *superblock = (struct wfs_sb *) disk_maps[0];
    int byte = 8;
    int block_n = indirect_block_location / byte;
    int block_off = indirect_block_location % byte;

    for (int i = 0; i < num_disks; i++) {

        char *disk = (char *) disk_maps[i];
        struct wfs_inode *other = (struct wfs_inode *)(superblock->i_blocks_ptr +disk + (inode->num * BLOCK_SIZE));
        void *indirect_block_ptr = disk + superblock->d_blocks_ptr + indirect_block_location * BLOCK_SIZE;
        memset(indirect_block_ptr, 0, BLOCK_SIZE);

        switch (superblock->raid_mode) {

            case 0:
                other->blocks[IND_BLOCK] = superblock->d_blocks_ptr + (indirect_block_location * BLOCK_SIZE);
                break;

            case 1:
            
            default:
                data_map =superblock-> d_bitmap_ptr + disk;

                int bits = (1 << block_off);
                data_map[block_n] = data_map[block_n] | bits;

                other->blocks[IND_BLOCK] = superblock->d_blocks_ptr + (indirect_block_location * BLOCK_SIZE);
                break;
        }
    }

    return 0;
}


int wfs_write_helper4(struct wfs_inode *inode, size_t indirect_offset, int new) {
    char *data_map;
    struct wfs_sb *superblock = (struct wfs_sb *) disk_maps[0];
    char *disk;
    uint32_t *other_ind_block;
    int bytes = 8;
    int block_off = new % bytes;
    int block_n = new / bytes;


    for (int i = 0; i < num_disks; i++) {

        disk = (char *) disk_maps[i];
        other_ind_block = (uint32_t *)(disk + inode->blocks[IND_BLOCK]);

        switch (superblock-> raid_mode) {
            case 0:
                other_ind_block[indirect_offset] = superblock->d_blocks_ptr + (new * BLOCK_SIZE);
                break;

            case 1:
            default:
                data_map = disk + superblock->d_bitmap_ptr;

                int bits = 1 << (block_off);
                data_map[block_n] = data_map[block_n] | bits;

                other_ind_block[indirect_offset] = superblock->d_blocks_ptr + (new * BLOCK_SIZE);
                break;
        }
    }

    return 0;
}


int wfs_write_helper5(int disk_index, size_t indirect_offset, size_t start_offset, const char *buf, size_t bytes_written, size_t to_write, uint32_t *indirect_block) {
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

        block_ptr = disk + start_offset + indirect_block[indirect_offset];

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


static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    struct wfs_inode *inode = lookup_inode(path, (char *)disk_maps[0]);

    if (!inode) {
        return -ENOENT;
    }

    if (!S_ISREG(inode->mode)) {
        return -EISDIR;
    }

    size_t bytes_written = 0;
    size_t block_offset = offset / BLOCK_SIZE;
    size_t start_offset = offset % BLOCK_SIZE;
    int vbn;
    int disk_index = 0;
    int new;
    size_t block_available_space;

    while (bytes_written < size) {
        vbn = block_offset;

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

                new = alloc_block((char *)disk_maps[disk_index]);
                if (new < 0) {
                    return -ENOSPC;
                }

                wfs_write_helper1(inode, block_offset, new, vbn);
            }

            block_available_space = BLOCK_SIZE - start_offset;
            size_t to_write;

             if(size - bytes_written < block_available_space) {
                to_write = size - bytes_written;
             } else {
                to_write = block_available_space;
             }

            wfs_write_helper2(inode, vbn, buf, bytes_written, to_write, block_offset, start_offset);

            bytes_written += to_write;
            block_offset++;
            start_offset = 0;

        } else {
            size_t indirect_offset = block_offset - D_BLOCK;

            if (inode->blocks[IND_BLOCK] == 0) {

                int indirect_block_location = alloc_block((char *)disk_maps[disk_index]);
                if (indirect_block_location < 0) {
                    return -ENOSPC;
                }

                wfs_write_helper3(inode, indirect_block_location);

            }

            uint32_t *indirect_block = (uint32_t *)((char *)disk_maps[disk_index] + inode->blocks[IND_BLOCK]);

            if (indirect_block[indirect_offset] == 0) {
                
                int new = alloc_block((char *)disk_maps[0]);

                if (new < 0) {
                    return -ENOSPC;
                }

                wfs_write_helper4(inode, indirect_offset, new);
            }

            size_t block_available_space = BLOCK_SIZE - start_offset;

            size_t to_write; 
            bool check1 = size - bytes_written < block_available_space;
            if(!check1) {
                to_write = block_available_space;
            } else if(check1) {
                to_write = size - bytes_written;
            }

            wfs_write_helper5(disk_index, indirect_offset, start_offset, buf, bytes_written, to_write, indirect_block);

            bytes_written += to_write;
            block_offset++;
            start_offset = 0;
        }
    }

    for (int i = 0; i < num_disks; i++) {

        char *disk = (char *)disk_maps[i];
        struct wfs_inode *other = (struct wfs_inode *)(disk + (inode->num * BLOCK_SIZE)+ superblock->i_blocks_ptr);
        other-> mtim = time(NULL);

        bool check2 = (offset + size > other->size);
        if(check2) {
            other-> size = offset + size;
        }

    }

    return bytes_written;
}


int extract_parent_and_name(const char *path, char *parent_filepath, char *file) {
    char temp_dir[28];
    strncpy(temp_dir, path, sizeof(temp_dir));
    char *parent = dirname(temp_dir);
    strncpy(parent_filepath, parent, 28);
    char temp_base[28];
    strncpy(temp_base, path, sizeof(temp_base));
    char *name = basename(temp_base);
    strncpy(file, name, MAX_NAME);
    return 0;
}


struct wfs_inode *get_parent_inode(const char *parent_filepath) {
    char* disk_char = (char *)disk_maps[0];

    struct wfs_inode *parent = lookup_inode(parent_filepath, disk_char);

    if (!parent) {
        return NULL;
    } else if (!S_ISDIR(parent-> mode)) {
        return NULL;
    } else {
        return parent;
    }
}


struct wfs_dentry *find_file_entry(struct wfs_inode *parent, const char *file) {

    struct wfs_dentry *entry;
    int i = 0;
    char* loc;
    int index;

    while (parent->blocks[i] != 0 && i < D_BLOCK) {

        loc = ((char *)disk_maps[0] + parent->blocks[i]);
        entry = (struct wfs_dentry *)(loc);

        for (int j = 0; j < BLOCK_SIZE; j+=sizeof(struct wfs_dentry)) {
            index = j/sizeof(struct wfs_dentry);
            if(strcmp(entry[index].name, file) != 0) {
                continue;
            }
            else if (strcmp(entry[index].name, file) == 0) {
                return &entry[index];
            }
        }
        i++;
    }

    // couldnt find it
    return NULL;
}


int free_file_blocks(int file_inum) {
    struct wfs_sb *superblock = (struct wfs_sb *) disk_maps[0];
    char *data_map;
    char *inode_map;
    int byte = 8;
    char *disk;
    int block_location;
    int block_n;
    int block_off;
    int bits;
    uint32_t *indirect_blocks;
    int indirect_block_location;

    for (int d = 0; d < num_disks; d++) {

        disk = (char *)disk_maps[d];
        data_map = disk + superblock->d_bitmap_ptr;
       inode_map = disk + superblock->i_bitmap_ptr;
        
        struct wfs_inode *f_inode = (struct wfs_inode *)(disk + superblock->i_blocks_ptr + (file_inum * BLOCK_SIZE));

        for (int i = 0; i < D_BLOCK; i++) {

            if (f_inode->blocks[i] == 0) {
                continue;
            } 

            else if (f_inode->blocks[i] != 0) {
                block_location = (f_inode-> blocks[i] - superblock->d_blocks_ptr);
                block_location /= BLOCK_SIZE;
                block_n = block_location /byte;
                block_off = block_location % byte;
                bits = ~(1 << block_off);

                f_inode->blocks[i] = 0;
                data_map[block_n] &= bits;
            }
        }


        if (f_inode-> blocks[IND_BLOCK] != 0) {

            indirect_blocks = (uint32_t *)(disk + f_inode-> blocks[IND_BLOCK]);

            for (int i = 0; i < BLOCK_SIZE; i+=sizeof(uint32_t)) {
                int index = i/sizeof(uint32_t);

                if (indirect_blocks[index] == 0) {
                    break;
                }

                else if (indirect_blocks[index] != 0) {

                    block_location = (indirect_blocks[index] - superblock ->d_blocks_ptr);
                    block_location /= BLOCK_SIZE;
                    block_n = block_location / byte;
                    block_off = (block_location % byte);
                    bits =  ~(1 << block_off);

                    data_map[block_n] &= bits;
                }
            }

            indirect_block_location = (f_inode->blocks[IND_BLOCK] - superblock -> d_blocks_ptr);
            indirect_block_location /= BLOCK_SIZE;
            int indirect_block_n = indirect_block_location / byte;
            int indirect_block_off = indirect_block_location % byte;
            bits = ~(1 << (indirect_block_off));

            data_map[indirect_block_n] &= bits;

            f_inode->blocks[IND_BLOCK] = 0;
        }

        memset(f_inode, 0, BLOCK_SIZE);

        int f_i_num = file_inum / byte;
        int f_i_off = file_inum % byte;
        bits = ~(1 << (f_i_off));

       inode_map[f_i_num] &= bits;
    }

    return 0;
}


int fill_other_inode(struct wfs_inode *parent_inode) {
    
    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    char* disk;

    for (int i = 0; i < num_disks; i++) {
        disk = (char *)disk_maps[i];

        struct wfs_inode *other = (struct wfs_inode *)(disk + (parent_inode->num * BLOCK_SIZE) + superblock->i_blocks_ptr);
        other->mtim = time(NULL);
        other->size =other->size - sizeof(struct wfs_dentry);
    }

    return 0;
}


int remove_directory_entry(struct wfs_inode *parent_inode, const char *fn) {

    char* disk;

    for (int i = 0; i < num_disks; i++) {

        disk = (char *)disk_maps[i];
        struct wfs_dentry *entry = (struct wfs_dentry *)(disk + parent_inode-> blocks[0]);

        for (int j = 0; j < BLOCK_SIZE; j+=sizeof(struct wfs_dentry)) {

            int index = j/sizeof(struct wfs_dentry);
            char* name = entry[index].name;

            if(strcmp(name, fn) != 0) {
                continue;
            }
            else if (strcmp(name, fn) == 0) {
                memset(&entry[index], 0, sizeof(struct wfs_dentry));
                break;
            }
        }
    }

    int rc;
    if (fill_other_inode(parent_inode) != 0) {
        rc = -1;
    }
    else {
        rc = 0;
    } 

    return rc;
}


static int wfs_unlink(const char *path) {
    char parent_filepath[28];
    char file[MAX_NAME];

    if(extract_parent_and_name(path, parent_filepath, file) >= 0) {

        struct wfs_inode *parent_inode = get_parent_inode(parent_filepath);
        if(parent_inode) {

            struct wfs_dentry *entry = find_file_entry(parent_inode, file);
            if (entry) {

                int file_inum = entry->num;
                if (free_file_blocks(file_inum) >= 0) {

                    if (remove_directory_entry(parent_inode, file) >= 0) {
                        return 0;
                    }
                    else {
                        return -EIO;
                    }
                }
                else {
                    return -EIO;
                }
            }
            else {
                return -ENOENT;
            }
        }
        else {
            return -ENOENT;
        }
    }
    else {
        return -EINVAL;
    }
}


int check_directory_empty(struct wfs_inode *dir_inode) {

    struct wfs_dentry *entries;
    char* disk = (char *)disk_maps[0];
    int index;
    int rc = 0;

    for (int i = 0; i < D_BLOCK; i++) {

        if (dir_inode-> blocks[i] == 0)  {
            continue;
        }

        else if (dir_inode-> blocks[i] != 0) {

            entries = (struct wfs_dentry *)(disk + dir_inode -> blocks[i]);

            for (int j = 0; j < BLOCK_SIZE; j+= sizeof(struct wfs_dentry)) {
                index = j/sizeof(struct wfs_dentry);

                if (entries[index].num != 0) {
                    if(strcmp(entries[index].name, ".") != 0 && strcmp(entries[index].name, "..") != 0) {
                        rc = -ENOTEMPTY;
                        break;
                    }
                    else continue;
                }
            }
        }
    }


    return rc;
}


void free_directory_blocks_and_inode(int dir_inode_num, struct wfs_inode *dir_inode) {

    struct wfs_sb *superblock = (struct wfs_sb *) disk_maps[0];
    char* disk;
    int block_location;
    char *data_map;
    char *inode_map;
    int byte = 8;
    int bits;
    int block_n;
    int block_off;

    for (int a = 0; a < num_disks; a++) {

        disk = (char *)disk_maps[a];
        data_map = disk + superblock->d_bitmap_ptr;
       inode_map = disk + superblock->i_bitmap_ptr;

        struct wfs_inode *other = (struct wfs_inode *)(disk + (dir_inode_num * BLOCK_SIZE)+ superblock -> i_blocks_ptr);

        for (int i = 0; i < D_BLOCK; i++) {
            if (other ->blocks[i] == 0) {
                continue;
            }
            else {
                block_location = (other->blocks[i] - superblock->d_blocks_ptr);
                block_location /= BLOCK_SIZE;

                if (superblock->raid_mode == 0 && block_location % num_disks != a) {
                    continue;
                }

                other->blocks[i] = 0;
                block_n = block_location / byte;
                block_off = block_location % byte;
                bits = ~(1 << (block_off));

                data_map[block_n] &= bits;
                  
            }
        }


        memset(other, 0, BLOCK_SIZE);
        block_n = dir_inode_num / byte;
        block_off = dir_inode_num % byte;
        bits = ~(1 << (block_off));

       inode_map[block_n] &= bits;
    }
}


struct wfs_dentry *locate_directory_entry(struct wfs_inode *parent_inode, const char *dir_name) {

    // struct wfs_dentry *entry;
    char* name;
    int index;
    char* disk = (char *)disk_maps[0];

    for (int i = 0; i < D_BLOCK; i++) {
        if (parent_inode->blocks[i] == 0) {
            break;
        }

        struct wfs_dentry *entries = (struct wfs_dentry *)(disk + parent_inode->blocks[i]);

        for (int j = 0; j < BLOCK_SIZE; j+=sizeof(struct wfs_dentry)) {
            name = entries[j].name;
            index = j/sizeof(struct wfs_dentry);

            if(strcmp(name, dir_name) != 0) {
                continue;
            }
            else if (strcmp(name, dir_name) == 0) {
                return &entries[index];
            }
        }
    }

    return NULL;
}


static int wfs_rmdir(const char *path) {

    struct wfs_sb *superblock = (struct wfs_sb *)disk_maps[0];
    int stride = sizeof(struct wfs_dentry);

    char parent_filepath[28];
    char name[MAX_NAME];
    if (extract_parent_and_name(path, parent_filepath, name) != 0) {
        return -EINVAL;
    }

    struct wfs_inode *parent_inode = get_parent_inode(parent_filepath);
    if (!parent_inode) {
        return -ENOENT;
    }

    struct wfs_dentry *entry = locate_directory_entry(parent_inode, name);
    if (!entry) {
        return -ENOENT;
    }

    int dir_inode_num = entry->num;
    struct wfs_inode *dir_inode = (struct wfs_inode *)((char *)disk_maps[0] + superblock->i_blocks_ptr + dir_inode_num * BLOCK_SIZE);

    int rc;
    if ((rc = check_directory_empty(dir_inode)) != 0) {
        return rc;
    }

    free_directory_blocks_and_inode(dir_inode_num, dir_inode);


    char* disk;
    int index;

    for (int i = 0; i < num_disks; i++) {

        disk = (char *) disk_maps[i];
        entry = (struct wfs_dentry *)(disk + parent_inode->blocks[0]);


        for (int j = 0; j < BLOCK_SIZE; j+= stride) {
            index = j/stride;

            if (strcmp(entry[index].name, name) == 0) {
                memset(&entry[index], 0, stride);
                break;
            }
            else if (strcmp(entry[index].name, name) != 0) {
                // printf("not a match: %s\n", entry[index].name);
                continue;
            }
        }
    }

    struct wfs_inode *other;
    for (int i = 0; i < num_disks; i++) {
        disk = (char *)disk_maps[i];

        other = (struct wfs_inode *)(disk +(parent_inode->num * BLOCK_SIZE) + superblock->i_blocks_ptr);
        other->mtim = time(NULL);
        other->size -= sizeof(struct wfs_dentry);
    }

    return 0;
}


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


int main(int argc, char *argv[]) {
    num_disks = 0;
    int fd;

    if (argc < 3) {
        return -1;
    }

    while (num_disks + 1 < argc && argv[num_disks + 1][0] != '-') {
        num_disks++;
    }

    for(int i=0; i<num_disks; i++) {
        fd = open(argv[i + 1], O_RDWR);
        struct stat temp;
        int checker = fstat(fd, &temp);
        if(checker == 0) {
            disk_maps[i] = mmap(NULL, temp.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (disk_maps[i] == MAP_FAILED) {
                close(fd);
                exit(-1);
            }
        }
        else {
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

    return fuse_main(fuse_argc, fuse_argv, &ops, NULL);
}