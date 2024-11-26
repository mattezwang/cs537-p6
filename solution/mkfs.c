#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include "wfs.h"

#define BLOCK_SIZE 512 

int raid_mode;
int num_inodes;
char **disk_images;
int capacity = 10;
int num_disks;
int num_data_blocks;


#include <time.h>

void initialize_disks() {
    struct wfs_sb sb;
    // Superblock Initialization
    sb.num_inodes = num_inodes;
    sb.num_data_blocks = num_data_blocks;
    sb.i_bitmap_ptr = sizeof(struct wfs_sb);
    sb.d_bitmap_ptr = sb.i_bitmap_ptr + ((num_inodes + 7) / 8); // Rounded inode bitmap
    sb.i_blocks_ptr = sb.d_bitmap_ptr + ((num_data_blocks + 7) / 8); // Rounded data bitmap
    sb.d_blocks_ptr = sb.i_blocks_ptr + (num_inodes * BLOCK_SIZE); // Aligned inode space

    size_t required_size = sb.d_blocks_ptr + (num_data_blocks * BLOCK_SIZE);

    // Initialize each disk image
    for (int i = 0; i < num_disks; i++) {
        int fd = open(disk_images[i], O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            perror("Error opening disk image");
            exit(EXIT_FAILURE);
        }

        // Ensure the disk is large enough
        if (ftruncate(fd, required_size) < 0) {
            perror("Error setting disk size");
            close(fd);
            exit(EXIT_FAILURE);
        }

        // Write superblock
        if (write(fd, &sb, sizeof(struct wfs_sb)) != sizeof(struct wfs_sb)) {
            perror("Error writing superblock");
            close(fd);
            exit(EXIT_FAILURE);
        }

        // Write empty inode bitmap
        size_t i_bitmap_size = (num_inodes + 7) / 8;
        char *bitmap = calloc(1, i_bitmap_size);
        if (!bitmap) {
            perror("Error allocating memory for inode bitmap");
            close(fd);
            exit(EXIT_FAILURE);
        }
        if (write(fd, bitmap, i_bitmap_size) != i_bitmap_size) {
            perror("Error writing inode bitmap");
            free(bitmap);
            close(fd);
            exit(EXIT_FAILURE);
        }

        // Write empty data bitmap
        size_t d_bitmap_size = (num_data_blocks + 7) / 8;
        if (write(fd, bitmap, d_bitmap_size) != d_bitmap_size) {
            perror("Error writing data bitmap");
            free(bitmap);
            close(fd);
            exit(EXIT_FAILURE);
        }
        free(bitmap);

        // Write root inode
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

    initialize_disks();

    // Free allocated memory
    for (int i = 0; i < num_disks; i++) {
        free(disk_images[i]);
    }
    free(disk_images);

    return 0;
}
