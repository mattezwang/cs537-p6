#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

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
    int inode_region_size = num_inodes * 512; // Each inode takes 512 bytes
    int data_region_size = num_data_blocks * 512;

    // Calculate required size for the disk
    int required_size = sizeof(superblock_t)       // Superblock
                        + inode_bitmap_size       // Inode bitmap
                        + data_bitmap_size        // Data bitmap
                        + inode_region_size       // Inode region
                        + data_region_size;       // Data region

    char *zero_block = calloc(1, 512); // A zeroed-out 512-byte block

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
            free(zero_block);
            exit(255);
        }

        // Write the superblock
        superblock_t sb = {
            .raid_mode = raid_mode,
            .num_inodes = num_inodes,
            .num_data_blocks = num_data_blocks,
            .inode_bitmap_start = sizeof(superblock_t),
            .data_bitmap_start = sizeof(superblock_t) + inode_bitmap_size,
            .inode_start = sizeof(superblock_t) + inode_bitmap_size + data_bitmap_size,
            .data_start = sizeof(superblock_t) + inode_bitmap_size + data_bitmap_size + inode_region_size
        };

        lseek(fd, 0, SEEK_SET);
        if (write(fd, &sb, sizeof(superblock_t)) != sizeof(superblock_t)) {
            perror("Failed to write superblock");
            close(fd);
            free(zero_block);
            exit(255);
        }

        // Write zeroed-out inode bitmap
        lseek(fd, sb.inode_bitmap_start, SEEK_SET);
        if (write(fd, zero_block, inode_bitmap_size) != inode_bitmap_size) {
            perror("Failed to write inode bitmap");
            close(fd);
            free(zero_block);
            exit(255);
        }

        // Write zeroed-out data bitmap
        lseek(fd, sb.data_bitmap_start, SEEK_SET);
        if (write(fd, zero_block, data_bitmap_size) != data_bitmap_size) {
            perror("Failed to write data bitmap");
            close(fd);
            free(zero_block);
            exit(255);
        }

        // Write zeroed-out inodes
        lseek(fd, sb.inode_start, SEEK_SET);
        printf("num_inodes = %i\n", num_inodes);

        for (int j = 0; j < num_inodes; j++) {
            printf("how many times does this run \n)");
            if (write(fd, zero_block, 512) != 512) {
                perror("Failed to write inode");
                close(fd);
                free(zero_block);
                exit(255);
            }
        }

        // Write zeroed-out data blocks
        lseek(fd, sb.data_start, SEEK_SET);
        for (int j = 0; j < num_data_blocks; j++) {
            if (write(fd, zero_block, 512) != 512) {
                perror("Failed to write data block");
                close(fd);
                free(zero_block);
                exit(255);
            }
        }

        close(fd);
    }
    free(zero_block);

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
