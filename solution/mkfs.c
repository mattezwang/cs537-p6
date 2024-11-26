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
    // Calculate bitmap sizes based on number of blocks
    int inode_bitmap_size = (num_inodes + 7) / 8; // Inode bitmap size in bytes
    int data_bitmap_size = (num_data_blocks + 7) / 8; // Data bitmap size in bytes

    // Align bitmap sizes to block boundaries
    int inode_bitmap_blocks = (inode_bitmap_size + BLOCK_SIZE - 1) / BLOCK_SIZE; // Blocks for inode bitmap
    int data_bitmap_blocks = (data_bitmap_size + BLOCK_SIZE - 1) / BLOCK_SIZE;   // Blocks for data bitmap

    // Calculate required disk size
    int required_size = sizeof(superblock_t)                     // Superblock
                        + inode_bitmap_blocks * BLOCK_SIZE       // Inode bitmap
                        + data_bitmap_blocks * BLOCK_SIZE        // Data bitmap
                        + num_inodes * BLOCK_SIZE                // Inodes region
                        + num_data_blocks * BLOCK_SIZE;          // Data blocks region

    for (int i = 0; i < num_disks; i++) {
        int fd = open(disk_images[i], O_RDWR | O_CREAT, 0666);
        if (fd < 0) {
            perror("Failed to open disk image");
            exit(255);
        }

        if (ftruncate(fd, required_size) < 0) {
            perror("Failed to set disk image size");
            close(fd);
            exit(255);
        }

        // Correct layout of the filesystem
        superblock_t sb = {
            .raid_mode = raid_mode,
            .num_inodes = num_inodes,
            .num_data_blocks = num_data_blocks,
            .inode_bitmap_start = 1,                                  // Block 1
            .data_bitmap_start = 1 + inode_bitmap_blocks,             // After inode bitmap
            .inode_start = 1 + inode_bitmap_blocks + data_bitmap_blocks, // After data bitmap
            .data_start = 1 + inode_bitmap_blocks + data_bitmap_blocks + num_inodes // After inodes
        };

        // Write the superblock to block 0
        lseek(fd, 0, SEEK_SET);
        if (write(fd, &sb, sizeof(superblock_t)) != sizeof(superblock_t)) {
            perror("Failed to write superblock");
            close(fd);
            exit(255);
        }

        // Write zeroed-out inode bitmap
        char *inode_bitmap = calloc(1, inode_bitmap_size); // Allocate memory for inode bitmap
        lseek(fd, sb.inode_bitmap_start * BLOCK_SIZE, SEEK_SET);
        if (write(fd, inode_bitmap, inode_bitmap_size) != inode_bitmap_size) {
            perror("Failed to write inode bitmap");
            free(inode_bitmap);
            close(fd);
            exit(255);
        }
        free(inode_bitmap);

        // Write zeroed-out data bitmap
        char *data_bitmap = calloc(1, data_bitmap_size); // Allocate memory for data bitmap
        lseek(fd, sb.data_bitmap_start * BLOCK_SIZE, SEEK_SET);
        if (write(fd, data_bitmap, data_bitmap_size) != data_bitmap_size) {
            perror("Failed to write data bitmap");
            free(data_bitmap);
            close(fd);
            exit(255);
        }
        free(data_bitmap);

        // Write zeroed-out inodes
        for (int j = 0; j < num_inodes; j++) {
            char *inode_block = calloc(1, BLOCK_SIZE);
            lseek(fd, (sb.inode_start + j) * BLOCK_SIZE, SEEK_SET);
            if (write(fd, inode_block, BLOCK_SIZE) != BLOCK_SIZE) {
                perror("Failed to write inode block");
                free(inode_block);
                close(fd);
                exit(255);
            }
            free(inode_block);
        }

        // Write zeroed-out data blocks
        for (int j = 0; j < num_data_blocks; j++) {
            char *data_block = calloc(1, BLOCK_SIZE);
            lseek(fd, (sb.data_start + j) * BLOCK_SIZE, SEEK_SET);
            if (write(fd, data_block, BLOCK_SIZE) != BLOCK_SIZE) {
                perror("Failed to write data block");
                free(data_block);
                close(fd);
                exit(255);
            }
            free(data_block);
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
