#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>

int raid_mode;
int num_inodes;
char** disk_images;
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
} superblock;

typedef struct {
    uint32_t size;       // File size in bytes
    uint16_t type;       // File type (e.g., directory = 1, file = 2)
    uint16_t links;      // Number of links (e.g., . and .. for a directory)
    uint32_t direct[10]; // Direct block pointers
    uint32_t indirect;   // Indirect block pointer
} inode_t;


void calculate_layout(superblock *sb) {
    sb->inode_bitmap_start = 1; // Superblock takes the first block
    sb->data_bitmap_start = sb->inode_bitmap_start + (num_inodes + 7) / 8 / 512;
    sb->inode_start = sb->data_bitmap_start + (num_data_blocks + 7) / 8 / 512;
    sb->data_start = sb->inode_start + num_inodes;
}

void validate_and_initialize_disks(int required_size) {
    for (int i = 0; i < num_disks; i++) {
        int fd = open(disk_images[i], O_RDWR | O_CREAT, 0644);
        if (fd < 0) {
            perror("Failed to open or create disk image");
            exit(-1);
        }

        // Check if the disk is large enough
        off_t size = lseek(fd, 0, SEEK_END);
        if (size < required_size) {
            printf("Resizing disk image %s to %d bytes\n", disk_images[i], required_size);
            if (ftruncate(fd, required_size) < 0) {
                perror("Failed to resize disk image");
                close(fd);
                exit(-1);
            }
        }

        // Zero out the disk
        char zero[512] = {0};
        lseek(fd, 0, SEEK_SET);
        for (int j = 0; j < required_size / 512; j++) {
            write(fd, zero, 512);
        }

        close(fd);
    }
}

void write_metadata(superblock *sb) {
    for (int i = 0; i < num_disks; i++) {
        int fd = open(disk_images[i], O_RDWR);
        if (fd < 0) {
            perror("Failed to open disk image");
            exit(-1);
        }

        // Write superblock
        lseek(fd, 0, SEEK_SET);
        write(fd, sb, sizeof(superblock));

        // Write inode bitmap
        char bitmap[512] = {0};
        bitmap[0] = 1; // Mark the root inode as allocated
        lseek(fd, sb->inode_bitmap_start * 512, SEEK_SET);
        write(fd, bitmap, 512);

        // Write data block bitmap
        lseek(fd, sb->data_bitmap_start * 512, SEEK_SET);
        write(fd, bitmap, 512);

        // Write empty inodes
        char inode_block[512] = {0};
        for (int j = 0; j < sb->num_inodes; j++) {
            lseek(fd, (sb->inode_start + j) * 512, SEEK_SET);
            write(fd, inode_block, 512);
        }

        // Write root inode
        inode_t root_inode = {0};
        root_inode.type = 1;  // Directory type
        root_inode.links = 2; // "." and ".."
        char root_block[512] = {0};
        memcpy(root_block, &root_inode, sizeof(inode_t));
        lseek(fd, sb->inode_start * 512, SEEK_SET);
        write(fd, root_block, 512);

        close(fd);
    }
}



void
parse_arguments(int argc, char *argv[]) {

    int tag;
    while ((tag = getopt(argc, argv, "r:d:i:b:")) != -1) {

        switch (tag) {
            case 'r':
                raid_mode = atoi(optarg);
                if (raid_mode > 1 || raid_mode < 0) {
                    fprintf(stderr, "raid mode out of bounds (has to be 0 or 1)\n");
                    exit(-1);
                }
                break;

            case 'd':
                if (num_disks >= capacity) {
                    capacity *= 2;
                    disk_images = realloc(disk_images, (capacity) * sizeof(char*));
                }
                if(disk_images == NULL) {
                    fprintf(stderr, "disk_images array allocation failed\n");
                    exit(-1);
                }

                disk_images[num_disks++] = strdup(optarg);
                break;

            case 'i':
                num_inodes = atoi(optarg);
                if (num_inodes <= 0) {
                    fprintf(stderr, "num of inodes has to be >= 0\n");
                    exit(-1);
                }
                break;

            case 'b':
                num_data_blocks = atoi(optarg);
                if (num_data_blocks <= 0) {
                    fprintf(stderr, "num of data blocks has to be >= 0\n");
                    exit(-1);
                }

                // this makes it a multiple of 32
                num_data_blocks = (num_data_blocks + 31) & ~31;
                break;

            default:
                fprintf(stderr, "usage: %s -r <raid_mode> -d <disk> -i <inodes> -b <blocks>\n", argv[0]);
                exit(-1);
        }
    }
}


int main(int argc, char *argv[]) {

    disk_images = malloc(capacity * sizeof(char*));
    parse_arguments(argc, argv);

    // superblock sb;
    // sb.raid_mode = raid_mode;
    // sb.num_inodes = num_inodes;
    // sb.num_data_blocks = num_data_blocks;

    superblock sb;
    sb.raid_mode = raid_mode;
    sb.num_inodes = num_inodes;
    sb.num_data_blocks = num_data_blocks;

    // Calculate layout
    calculate_layout(&sb);

    // Calculate required size
    int required_size = (1 +                        // Superblock
                         (num_inodes + 7) / 8 / 512 +  // Inode bitmap
                         (num_data_blocks + 7) / 8 / 512 + // Data block bitmap
                         num_inodes +                  // Inodes
                         num_data_blocks) * 512;

    // Validate and initialize disk images
    validate_and_initialize_disks(required_size);

    // Write metadata to disk
    write_metadata(&sb);


    for (int i = 0; i < num_disks; i++) {
        free(disk_images[i]);
    }
    free(disk_images);

    return 0;

}  