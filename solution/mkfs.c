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


void initialize_superblock(struct wfs_sb *sb, int raid_mode, int num_inodes, int num_data_blocks) {
    sb->num_inodes = num_inodes;
    sb->num_data_blocks = num_data_blocks;
    sb->i_bitmap_ptr = sizeof(struct wfs_sb);
    sb->d_bitmap_ptr = sb->i_bitmap_ptr + ((num_inodes + 7) / 8); // Round up for bytes
    sb->i_blocks_ptr = sb->d_bitmap_ptr + ((num_data_blocks + 7) / 8); // Round up for bytes
    sb->d_blocks_ptr = sb->i_blocks_ptr + (num_inodes * BLOCK_SIZE);
}

void initialize_root_inode(struct wfs_inode *root_inode) {
    memset(root_inode, 0, sizeof(struct wfs_inode));
    root_inode->num = 0; // Root inode number
    root_inode->mode = S_IFDIR | 0755; // Directory with rwxr-xr-x permissions
    root_inode->uid = 0; // Root user
    root_inode->gid = 0; // Root group
    root_inode->nlinks = 2; // Parent (`.`) and self (`..`)
    root_inode->atim = root_inode->mtim = root_inode->ctim = time(NULL);
}

void write_superblock(int fd, struct wfs_sb *sb) {
    if (write(fd, sb, sizeof(struct wfs_sb)) != sizeof(struct wfs_sb)) {
        perror("Error writing superblock");
        exit(EXIT_FAILURE);
    }
}

void write_bitmap(int fd, size_t size) {
    char *bitmap = calloc(1, size);
    if (!bitmap) {
        perror("Error allocating memory for bitmap");
        exit(EXIT_FAILURE);
    }
    if (write(fd, bitmap, size) != size) {
        perror("Error writing bitmap");
        free(bitmap);
        exit(EXIT_FAILURE);
    }
    free(bitmap);
}

void write_root_inode(int fd) {
    struct wfs_inode root_inode;
    initialize_root_inode(&root_inode);
    if (write(fd, &root_inode, sizeof(struct wfs_inode)) != sizeof(struct wfs_inode)) {
        perror("Error writing root inode");
        exit(EXIT_FAILURE);
    }
}

void setup_disk(const char *disk_image, struct wfs_sb *sb) {
    int fd = open(disk_image, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Error opening disk image");
        exit(EXIT_FAILURE);
    }

    size_t required_size = sb->d_blocks_ptr + (sb->num_data_blocks * BLOCK_SIZE);
    if (ftruncate(fd, required_size) < 0) {
        perror("Error setting disk size");
        close(fd);
        exit(EXIT_FAILURE);
    }

    write_superblock(fd, sb);

    // Write inode and data bitmaps
    write_bitmap(fd, (sb->num_inodes + 7) / 8);
    write_bitmap(fd, (sb->num_data_blocks + 7) / 8);

    // Write root inode
    write_root_inode(fd);

    close(fd);
}

int main(int argc, char *argv[]) {
    parse_arguments(argc, argv);

    struct wfs_sb sb;
    initialize_superblock(&sb, raid_mode, num_inodes, num_data_blocks);

    for (int i = 0; i < num_disks; i++) {
        setup_disk(disk_images[i], &sb);
    }

    return 0;
}