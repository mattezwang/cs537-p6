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

        // Write empty inode bitmap
        lseek(fd, sb->inode_bitmap_start * 512, SEEK_SET);
        char bitmap[512] = {0};
        bitmap[0] = 1; // Mark the root inode in the bitmap
        write(fd, bitmap, 512);

        // Write empty data bitmap
        lseek(fd, sb->data_bitmap_start * 512, SEEK_SET);
        for (int j = 0; j < (num_data_blocks + 7) / 8 / 512; j++) {
            write(fd, bitmap, 512);
        }

        // Write all inodes
        char inode[512] = {0};
        for (int j = 0; j < sb->num_inodes; j++) {
            lseek(fd, (sb->inode_start + j) * 512, SEEK_SET);
            write(fd, inode, 512);
        }

        // Mark and write the root inode
        char root_inode[512] = {0};
        root_inode[0] = 1; // Mark the root inode as allocated
        lseek(fd, sb->inode_start * 512, SEEK_SET);
        write(fd, root_inode, 512);

        close(fd);
    }
}



void validate_and_initialize_disks() {
    for (int i = 0; i < num_disks; i++) {
        int fd = open(disk_images[i], O_RDWR | O_CREAT, 0644);
        if (fd < 0) {
            perror("Failed to open disk image");
            exit(-1);
        }

        // Calculate required size
        int required_size = (1 +   // Superblock
                             (num_inodes + 7) / 8 / 512 +  // Inode bitmap
                             (num_data_blocks + 7) / 8 / 512 + // Data block bitmap
                             num_inodes +  // Inodes
                             num_data_blocks) * 512;

        // Check if disk is large enough
        off_t size = lseek(fd, 0, SEEK_END);
        if (size < required_size) {
            fprintf(stderr, "Disk image %s is too small\n", disk_images[i]);
            exit(-1);
        }

        // Zero out the file
        lseek(fd, 0, SEEK_SET);
        char zero[512] = {0};
        for (int j = 0; j < required_size / 512; j++) {
            write(fd, zero, 512);
        }

        close(fd);
    }
}



void calculate_layout(superblock *sb) {
    sb->inode_bitmap_start = 1; // Superblock takes the first block
    sb->data_bitmap_start = sb->inode_bitmap_start + (num_inodes + 7) / 8 / 512;
    sb->inode_start = sb->data_bitmap_start + (num_data_blocks + 7) / 8 / 512;
    sb->data_start = sb->inode_start + num_inodes;
}




int main(int argc, char *argv[]) {
    disk_images = malloc(capacity * sizeof(char*));
    parse_arguments(argc, argv);

    superblock sb;
    sb.raid_mode = raid_mode;
    sb.num_inodes = num_inodes;
    sb.num_data_blocks = num_data_blocks;

    calculate_layout(&sb);
    validate_and_initialize_disks();
    write_metadata(&sb);

    // Free resources
    for (int i = 0; i < num_disks; i++) {
        free(disk_images[i]);
    }
    free(disk_images);

    return 0;

}  