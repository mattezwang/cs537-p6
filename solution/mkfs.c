#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>

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

typedef struct {
    int mode;    // File mode (directory, regular file, etc.)
    int size;    // File size in bytes
    int direct[10]; // Direct block pointers
    int indirect;   // Indirect block pointer
} inode_t;

void parse_arguments(int argc, char *argv[]) {
    int tag;
    while ((tag = getopt(argc, argv, "r:d:i:b:")) != -1) {
        switch (tag) {
            case 'r':
                raid_mode = atoi(optarg);
                if (raid_mode > 1 || raid_mode < 0) {
                    fprintf(stderr, "RAID mode out of bounds (must be 0 or 1)\n");
                    exit(255);
                }
                break;
            case 'd':
                if (num_disks >= capacity) {
                    capacity *= 2;
                    disk_images = realloc(disk_images, capacity * sizeof(char *));
                }
                if (disk_images == NULL) {
                    fprintf(stderr, "Disk images array allocation failed\n");
                    exit(255);
                }
                disk_images[num_disks++] = strdup(optarg);
                break;
            case 'i':
                num_inodes = atoi(optarg);
                if (num_inodes <= 0) {
                    fprintf(stderr, "Number of inodes must be > 0\n");
                    exit(255);
                }
                break;
            case 'b':
                num_data_blocks = atoi(optarg);
                if (num_data_blocks <= 0) {
                    fprintf(stderr, "Number of data blocks must be > 0\n");
                    exit(255);
                }
                num_data_blocks = (num_data_blocks + 31) & ~31; // Round up to multiple of 32
                break;
            default:
                fprintf(stderr, "Usage: %s -r <raid_mode> -d <disk> -i <inodes> -b <blocks>\n", argv[0]);
                exit(255);
        }
    }
}

void initialize_filesystem() {
    int superblock_size = sizeof(superblock_t);
    int inode_bitmap_size = (num_inodes + 7) / 8; // 1 bit per inode
    int data_bitmap_size = (num_data_blocks + 7) / 8; // 1 bit per data block
    int inode_region_size = num_inodes * 512; // Each inode is aligned to 512 bytes
    int data_region_size = num_data_blocks * 512;

    int required_size = superblock_size + inode_bitmap_size + data_bitmap_size + inode_region_size + data_region_size;

    for (int i = 0; i < num_disks; i++) {
        int fd = open(disk_images[i], O_RDWR | O_CREAT, 0666);
        if (fd < 0) {
            perror("Failed to open disk image");
            exit(255);
        }

        struct stat st;
        fstat(fd, &st);
        if (st.st_size < required_size) {
            fprintf(stderr, "Disk image %s is too small for the filesystem\n", disk_images[i]);
            close(fd);
            exit(255);
        }

        // Write the superblock
        superblock_t sb = {
            .raid_mode = raid_mode,
            .num_inodes = num_inodes,
            .num_data_blocks = num_data_blocks,
            .inode_bitmap_start = superblock_size,
            .data_bitmap_start = superblock_size + inode_bitmap_size,
            .inode_start = superblock_size + inode_bitmap_size + data_bitmap_size,
            .data_start = superblock_size + inode_bitmap_size + data_bitmap_size + inode_region_size
        };
        lseek(fd, 0, SEEK_SET);
        write(fd, &sb, sizeof(superblock_t));

        // Write zeroed-out bitmaps
        char *zero_bitmap = calloc(1, inode_bitmap_size > data_bitmap_size ? inode_bitmap_size : data_bitmap_size);
        lseek(fd, sb.inode_bitmap_start, SEEK_SET);
        write(fd, zero_bitmap, inode_bitmap_size);
        lseek(fd, sb.data_bitmap_start, SEEK_SET);
        write(fd, zero_bitmap, data_bitmap_size);
        free(zero_bitmap);

        // Write root inode
        inode_t root_inode = {
            .mode = S_IFDIR | 0755, // Directory with permissions
            .size = 0,
            .direct = {0}, // All direct pointers zeroed
            .indirect = 0
        };
        lseek(fd, sb.inode_start, SEEK_SET);
        write(fd, &root_inode, sizeof(inode_t));

        close(fd);
    }
}

int main(int argc, char *argv[]) {
    disk_images = malloc(capacity * sizeof(char *));
    parse_arguments(argc, argv);
    initialize_filesystem();

    for (int i = 0; i < num_disks; i++) {
        free(disk_images[i]);
    }
    free(disk_images);

    return 0;
}
