#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "wfs.h"

int raid_mode;
int num_inodes;
char **disk_images;
int capacity;
int num_disks;
int num_data_blocks;

//function to initalize super block
void init_sb(struct wfs_sb *sb) {
    size_t inode_bitmap_bytes = (num_inodes + 7) / 8;
    size_t data_bitmap_bytes = (num_data_blocks + 7) / 8;

    //Calculate offsets
    size_t sb_size = sizeof(struct wfs_sb);
    off_t i_bitmap_off = sb_size;
    off_t d_bitmap_off = i_bitmap_off + inode_bitmap_bytes;
    off_t inode_start = d_bitmap_off + data_bitmap_bytes;

    // align inode blocks
    inode_start = ((inode_start + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE;

    //find data blocks start
    off_t db_start = inode_start + (num_inodes * BLOCK_SIZE);

    // fill supernode with info
    sb->num_inodes = num_inodes;
    sb->num_data_blocks = num_data_blocks;
    sb->i_bitmap_ptr = i_bitmap_off;
    sb->d_bitmap_ptr = d_bitmap_off;
    sb->i_blocks_ptr = inode_start;
    sb->d_blocks_ptr = db_start;
    sb->raid_mode = raid_mode;
    sb->num_disks = num_disks;
}


void init_disks() {
    struct wfs_sb sb;
    init_sb((struct wfs_sb *)&sb);

    //Calculate total filesystem size
    size_t fs_size = sb.d_blocks_ptr + (num_data_blocks * BLOCK_SIZE);

    int *file_descs = malloc(num_disks * sizeof(int));
    void **disk_maps = malloc(num_disks * sizeof(void *));

    for (int i = 0; i < num_disks; i++) {
        file_descs[i] = open(disk_images[i], O_RDWR);
        if (file_descs[i] < 0) {
            // perror("Error opening disk image");
            exit(-1);
        }

        struct stat st;
        if (fstat(file_descs[i], &st) < 0 || st.st_size < fs_size) {
            // perror("Error with disk image size or stat");
            exit(-1);
        }

        //Map disk image
        disk_maps[i] = mmap(NULL, fs_size, PROT_READ | PROT_WRITE, MAP_SHARED, file_descs[i], 0);
        if (disk_maps[i] == MAP_FAILED) {
            // perror("Error mapping disk image");
            exit(-1);
        }

        //Zero out the disk and write superblock
        memset(disk_maps[i], 0, fs_size);
        memcpy(disk_maps[i], &sb, sizeof(sb));

        //Write inode bitmap
        char *inode_bitmap = (char *)disk_maps[i] + sb.i_bitmap_ptr;
        memset(inode_bitmap, 0, (num_inodes + 7) / 8);
        inode_bitmap[0] = 1;  //Mark first inode as used

        //Write data bitmap
        char *data_bitmap = (char *)disk_maps[i] + sb.d_bitmap_ptr;
        memset(data_bitmap, 0, (num_data_blocks + 7) / 8);

        //Write root inode
        struct wfs_inode root = {0};
        root.mode = S_IFDIR | 0755;
        root.uid = getuid();
        root.gid = getgid();
        root.nlinks = 2;
        root.atim = time(NULL);
        root.mtim = time(NULL);
        root.ctim = time(NULL);
        memcpy((char *)disk_maps[i] + sb.i_blocks_ptr, &root, sizeof(root));

        //Sync changes to disk
        msync(disk_maps[i], fs_size, MS_SYNC);

        //Cleanup mapping
        munmap(disk_maps[i], fs_size);
        close(file_descs[i]);
    }

    free(file_descs);
    free(disk_maps);
}


// get the arguments from the command line using opt
void parse_arguments(int argc, char *argv[]) {
    int tag;
    while ((tag = getopt(argc, argv, "r:d:i:b:")) != -1) {
        switch (tag) {
            case 'r':
                raid_mode = atoi(optarg);
                if (raid_mode > 1 || raid_mode < 0) {
                    // fprintf(stderr, "raid mode out of bounds (has to be 0 or 1)\n");
                    exit(1);
                }
                break;

            case 'd':
                if (num_disks >= capacity) {
                    capacity *= 2;
                    disk_images = realloc(disk_images, (capacity) * sizeof(char *));
                }
                if (disk_images == NULL) {
                    // fprintf(stderr, "disk_images array allocation failed\n");
                    exit(1);
                }
                disk_images[num_disks++] = strdup(optarg);
                break;

            case 'i':
                num_inodes = atoi(optarg);
                if (num_inodes <= 0) {
                    // fprintf(stderr, "num of inodes has to be >= 0\n");
                    exit(1);
                }
                break;

            case 'b':
                num_data_blocks = atoi(optarg);
                if (num_data_blocks <= 0) {
                    // fprintf(stderr, "num of data blocks has to be >= 0\n");
                    exit(1);
                }
                break;

            default:
                // fprintf(stderr, "usage: %s -r <raid_mode> -d <disk> -i <inodes> -b <blocks>\n", argv[0]);
                exit(1);
        }
    }

    if (num_disks < 2) {
        // fprintf(stderr, "at least 2 disks are needed.\n");
        exit(1);
    }

    //this makes it a multiple of 32
    num_data_blocks = (num_data_blocks + 31) & ~31;
    num_inodes = (num_inodes + 31) & ~31;

}


int main(int argc, char *argv[]) {
    capacity = 256;
    disk_images = malloc(capacity * sizeof(char *));
    
    parse_arguments(argc, argv);
    init_disks();

    // free memory
    for (int i = 0; i < num_disks; i++) {
        free(disk_images[i]);
    }

    free(disk_images);

    return 0;
}