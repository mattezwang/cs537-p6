#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include "wfs.h"

int raid_mode;
int num_inodes;
char **disk_images;
int capacity = 10;
int num_disks;
int num_data_blocks;


void init_sb(struct wfs_sb *sb) {
    sb->num_inodes = num_inodes;
    sb->num_data_blocks = num_data_blocks;

    // assigns the inode bitmap
    sb->i_bitmap_ptr = sizeof(struct wfs_sb);
    // aligns it
    // sb->i_bitmap_ptr = (sb->i_bitmap_ptr + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);

    sb->d_bitmap_ptr = sb->i_bitmap_ptr + ((num_inodes + 7) / 8);
    // sb->d_bitmap_ptr = (sb->d_bitmap_ptr + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);

    sb->i_blocks_ptr = sb->d_bitmap_ptr + ((num_data_blocks + 7) / 8);
    sb->i_blocks_ptr = (sb->i_blocks_ptr + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);

    sb->d_blocks_ptr = sb->i_blocks_ptr + (num_inodes * BLOCK_SIZE);
    sb->d_blocks_ptr = (sb->d_blocks_ptr + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);


    size_t required_size = sizeof(struct wfs_sb)
                     + ((num_inodes + 7) / 8)
                     + ((num_data_blocks + 7) / 8)
                     + (num_inodes * BLOCK_SIZE)
                     + (num_data_blocks * BLOCK_SIZE);

    // Calculate sizes using offsets
    size_t inode_bitmap_size = sb->d_bitmap_ptr - sb->i_bitmap_ptr;
    size_t data_bitmap_size = sb->i_blocks_ptr - sb->d_bitmap_ptr;
    size_t inode_region_size = sb->d_blocks_ptr - sb->i_blocks_ptr;
    size_t data_block_region_size = required_size - sb->d_blocks_ptr; 

    // Print debug information
    printf("Filesystem Layout:\n");
    printf("  Superblock size: %zu bytes\n", sizeof(struct wfs_sb));
    printf("  Inode bitmap offset: %ld bytes, size: %ld bytes\n", sb->i_bitmap_ptr, inode_bitmap_size);
    printf("  Data bitmap offset: %ld bytes, size: %ld bytes\n", sb->d_bitmap_ptr, data_bitmap_size);
    printf("  Inode region offset: %ld bytes, size: %ld bytes\n", sb->i_blocks_ptr, inode_region_size);
    printf("  Data block region offset: %ld bytes, size: %ld bytes\n", sb->d_blocks_ptr, data_block_region_size);
}

void init_disks() {
    struct wfs_sb sb;
    init_sb(&sb);

    // superblock size + inodes bitmap size + data bitmap size + inodes size + data blocks size
    size_t required_size = sizeof(struct wfs_sb)
                     + ((num_inodes + 7) / 8)
                     + ((num_data_blocks + 7) / 8)
                     + (num_inodes * BLOCK_SIZE)
                     + (num_data_blocks * BLOCK_SIZE);

    // aligning the required size
    required_size = (required_size + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);

    // iterating through the disks
    for (int i = 0; i < num_disks; i++) {

        // opening the disks
        int fd = open(disk_images[i], O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("Error opening disk image");
            exit(EXIT_FAILURE);
        }

        if (ftruncate(fd, required_size) < 0) {
            perror("Error setting disk size");
            close(fd);
            exit(EXIT_FAILURE);
        }

        // checks if entire superblock is written to the disk. if not, return failure
        if (write(fd, &sb, sizeof(struct wfs_sb)) != sizeof(struct wfs_sb)) {
            perror("Error writing superblock");
            close(fd);
            exit(EXIT_FAILURE);
        }

        // write empty inside the inode bitmap, clear out space
        size_t i_bitmap_size = (num_inodes + 7) / 8;
        uint8_t *inode_bitmap = calloc(1, i_bitmap_size);
        if (write(fd, inode_bitmap, i_bitmap_size) != i_bitmap_size) {
            perror("Error writing inode bitmap");
            close(fd);
            exit(EXIT_FAILURE);
        }
        inode_bitmap[0] |= 1;

        // do the same for the data bitmap
        size_t d_bitmap_size = (num_data_blocks + 7) / 8;
        if (write(fd, calloc(1, d_bitmap_size), d_bitmap_size) != d_bitmap_size) {
            perror("Error writing inode bitmap");
            close(fd);
            exit(EXIT_FAILURE);
        }

        struct wfs_inode root_inode = {0};
        root_inode.num = 0;
        root_inode.mode = S_IFDIR | 0755; // Directory permissions
        root_inode.uid = 0; // Root user
        root_inode.gid = 0; // Root group
        root_inode.nlinks = 2; // Links: "." and ".."
        root_inode.atim = root_inode.mtim = root_inode.ctim = time(NULL);
        if (write(fd, &root_inode, sizeof(struct wfs_inode)) != sizeof(struct wfs_inode)) {
            perror("Error writing root inode");
            close(fd);
            exit(EXIT_FAILURE);
        }

        close(fd);
    }
}


void parse_arguments(int argc, char *argv[]) {
    int tag;
    while ((tag = getopt(argc, argv, "r:d:i:b:")) != -1) {
        switch (tag) {
            case 'r':
                raid_mode = atoi(optarg);
                if (raid_mode > 1 || raid_mode < 0) {
                    fprintf(stderr, "raid mode out of bounds (has to be 0 or 1)\n");
                    exit(255);
                }
                break;

            case 'd':
                if (num_disks >= capacity) {
                    capacity *= 2;
                    disk_images = realloc(disk_images, (capacity) * sizeof(char *));
                }
                if (disk_images == NULL) {
                    fprintf(stderr, "disk_images array allocation failed\n");
                    exit(255);
                }
                disk_images[num_disks++] = strdup(optarg);
                break;

            case 'i':
                num_inodes = atoi(optarg);
                if (num_inodes <= 0) {
                    fprintf(stderr, "num of inodes has to be >= 0\n");
                    exit(255);
                }
                break;

            case 'b':
                num_data_blocks = atoi(optarg);
                if (num_data_blocks <= 0) {
                    fprintf(stderr, "num of data blocks has to be >= 0\n");
                    exit(255);
                }

                // this makes it a multiple of 32
                num_data_blocks = (num_data_blocks + 31) & ~31;
                break;

            default:
                fprintf(stderr, "usage: %s -r <raid_mode> -d <disk> -i <inodes> -b <blocks>\n", argv[0]);
                exit(255);
        }
    }
}


int main(int argc, char *argv[]) {
    disk_images = malloc(capacity * sizeof(char *));
    parse_arguments(argc, argv);

    init_disks();

    // Free allocated memory
    for (int i = 0; i < num_disks; i++) {
        free(disk_images[i]);
    }
    free(disk_images);

    return 0;
}
