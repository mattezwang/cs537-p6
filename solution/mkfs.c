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
                if(disk_images == -1) {
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
    disk_images = alloc(capacity * sizeof(char*));
    parse_arguments;

    printf("RAID Mode: %d\n", raid_mode);
    printf("Number of Inodes: %d\n", num_inodes);
    printf("Number of Data Blocks (rounded): %d\n", num_data_blocks);
    printf("Number of Disk Images: %d\n", num_disks);
    for (int i = 0; i < num_disks; i++) {
        printf("Disk Image %d: %s\n", i + 1, disk_images[i]);
    }
}  