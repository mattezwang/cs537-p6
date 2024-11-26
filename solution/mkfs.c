#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

#define BLOCK_SIZE 512 

int raid_mode;
int num_inodes;
char **disk_images;
int capacity = 10;
int num_disks;
int num_data_blocks;

typedef struct {
    int raid_mode;
    int num_inodes;
    int num_data_blocks;
    int inode_bitmap_start;
    int data_bitmap_start;
    int inode_start;
    int data_start;
} superblock_t;

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

void initialize_filesystem() {
    int inode_bitmap_size = (num_inodes + 7) / 8; // Bitmap size in bytes (1 bit per inode)
    int data_bitmap_size = (num_data_blocks + 7) / 8; // Bitmap size in bytes (1 bit per data block)
    int inode_region_size = num_inodes * BLOCK_SIZE; // Each inode takes 512 bytes
    int data_region_size = num_data_blocks * BLOCK_SIZE;

    // Calculate required size for the disk
    int required_size = sizeof(superblock_t)       // Superblock
                        + ((inode_bitmap_size + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE
                        + ((data_bitmap_size + BLOCK_SIZE - 1) / BLOCK_SIZE) * BLOCK_SIZE
                        + inode_region_size       // Inode region
                        + data_region_size;       // Data region

    for (int i = 0; i < num_disks; i++) {
        int fd = open(disk_images[i], O_RDWR | O_CREAT, 0666);
        if (fd < 0) {
            perror("Failed to open disk image");
            exit(255);
        }

        // Ensure the disk image has enough size
        if (ftruncate(fd, required_size) < 0) {
            perror("Failed to set disk image size");
            close(fd);
            exit(255);
        }

        // Initialize the superblock
        superblock_t sb = {
            .raid_mode = raid_mode,
            .num_inodes = num_inodes,
            .num_data_blocks = num_data_blocks,
            .inode_bitmap_start = 1, // Block 0 is the superblock
            .data_bitmap_start = 1 + (inode_bitmap_size + BLOCK_SIZE - 1) / BLOCK_SIZE,
            .inode_start = 1 + (inode_bitmap_size + BLOCK_SIZE - 1) / BLOCK_SIZE +
                           (data_bitmap_size + BLOCK_SIZE - 1) / BLOCK_SIZE,
            .data_start = 1 + (inode_bitmap_size + BLOCK_SIZE - 1) / BLOCK_SIZE +
                          (data_bitmap_size + BLOCK_SIZE - 1) / BLOCK_SIZE +
                          num_inodes // 1 block per inode
        };

        // Write the superblock
        lseek(fd, 0, SEEK_SET);
        if (write(fd, &sb, sizeof(superblock_t)) != sizeof(superblock_t)) {
            perror("Failed to write superblock");
            close(fd);
            exit(255);
        }

        // Write zeroed-out inode bitmap
        char *inode_bitmap = calloc(1, inode_bitmap_size); // Allocate memory for inode bitmap
        if (!inode_bitmap) {
            perror("Failed to allocate memory for inode bitmap");
            close(fd);
            exit(255);
        }
        lseek(fd, sb.inode_bitmap_start * BLOCK_SIZE, SEEK_SET);
        if (write(fd, inode_bitmap, inode_bitmap_size) != inode_bitmap_size) {
            perror("Failed to write inode bitmap");
            free(inode_bitmap);
            close(fd);
            exit(255);
        }
        free(inode_bitmap); // Free allocated memory for inode bitmap

        // Write zeroed-out data bitmap
        char *data_bitmap = calloc(1, data_bitmap_size); // Allocate memory for data bitmap
        if (!data_bitmap) {
            perror("Failed to allocate memory for data bitmap");
            close(fd);
            exit(255);
        }
        lseek(fd, sb.data_bitmap_start * BLOCK_SIZE, SEEK_SET);
        if (write(fd, data_bitmap, data_bitmap_size) != data_bitmap_size) {
            perror("Failed to write data bitmap");
            free(data_bitmap);
            close(fd);
            exit(255);
        }
        free(data_bitmap); // Free allocated memory for data bitmap

        // Write zeroed-out inodes
        for (int j = 0; j < num_inodes; j++) {
            char *inode_block = calloc(1, BLOCK_SIZE); // Allocate memory for each inode block
            if (!inode_block) {
                perror("Failed to allocate memory for inode block");
                close(fd);
                exit(255);
            }
            lseek(fd, (sb.inode_start + j) * BLOCK_SIZE, SEEK_SET);
            if (write(fd, inode_block, BLOCK_SIZE) != BLOCK_SIZE) {
                perror("Failed to write inode block");
                free(inode_block);
                close(fd);
                exit(255);
            }
            free(inode_block); // Free allocated memory for inode block
        }

        // Write zeroed-out data blocks
        for (int j = 0; j < num_data_blocks; j++) {
            char *data_block = calloc(1, BLOCK_SIZE); // Allocate memory for each data block
            if (!data_block) {
                perror("Failed to allocate memory for data block");
                close(fd);
                exit(255);
            }
            lseek(fd, (sb.data_start + j) * BLOCK_SIZE, SEEK_SET);
            if (write(fd, data_block, BLOCK_SIZE) != BLOCK_SIZE) {
                perror("Failed to write data block");
                free(data_block);
                close(fd);
                exit(255);
            }
            free(data_block); // Free allocated memory for data block
        }

        close(fd);
    }

    printf("Filesystem initialized successfully.\n");
}


int main(int argc, char *argv[]) {
    disk_images = malloc(capacity * sizeof(char *));
    parse_arguments(argc, argv);

    // Call the function to initialize the filesystem
    initialize_filesystem();

    // Free allocated memory
    for (int i = 0; i < num_disks; i++) {
        free(disk_images[i]);
    }
    free(disk_images);

    return 0;
}
