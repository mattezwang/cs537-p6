// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <fcntl.h>
// #include <stdint.h>

// #define BLOCK_SIZE 512 

// int raid_mode;
// int num_inodes;
// char **disk_images;
// int capacity = 10;
// int num_disks;
// int num_data_blocks;

// typedef struct {
//     int raid_mode;
//     int num_inodes;
//     int num_data_blocks;
//     int inode_bitmap_start;
//     int data_bitmap_start;
//     int inode_start;
//     int data_start;
// } superblock_t;

// void parse_arguments(int argc, char *argv[]) {
//     int tag;
//     while ((tag = getopt(argc, argv, "r:d:i:b:")) != -1) {
//         switch (tag) {
//             case 'r':
//                 raid_mode = atoi(optarg);
//                 if (raid_mode > 1 || raid_mode < 0) {
//                     fprintf(stderr, "raid mode out of bounds (has to be 0 or 1)\n");
//                     exit(255);
//                 }
//                 break;

//             case 'd':
//                 if (num_disks >= capacity) {
//                     capacity *= 2;
//                     disk_images = realloc(disk_images, (capacity) * sizeof(char *));
//                 }
//                 if (disk_images == NULL) {
//                     fprintf(stderr, "disk_images array allocation failed\n");
//                     exit(255);
//                 }
//                 disk_images[num_disks++] = strdup(optarg);
//                 break;

//             case 'i':
//                 num_inodes = atoi(optarg);
//                 if (num_inodes <= 0) {
//                     fprintf(stderr, "num of inodes has to be >= 0\n");
//                     exit(255);
//                 }
//                 break;

//             case 'b':
//                 num_data_blocks = atoi(optarg);
//                 if (num_data_blocks <= 0) {
//                     fprintf(stderr, "num of data blocks has to be >= 0\n");
//                     exit(255);
//                 }

//                 // this makes it a multiple of 32
//                 num_data_blocks = (num_data_blocks + 31) & ~31;
//                 break;

//             default:
//                 fprintf(stderr, "usage: %s -r <raid_mode> -d <disk> -i <inodes> -b <blocks>\n", argv[0]);
//                 exit(255);
//         }
//     }
// }





// int main(int argc, char *argv[]) {
//     disk_images = malloc(capacity * sizeof(char *));
//     parse_arguments(argc, argv);

    
//     // Free allocated memory
//     for (int i = 0; i < num_disks; i++) {
//         free(disk_images[i]);
//     }
//     free(disk_images);

//     return 0;
// }


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>

#define BLOCK_SIZE 512
#define MAX_DISKS 16

// Superblock structure
typedef struct wfs_sb {
    size_t num_inodes;
    size_t num_data_blocks;
    off_t i_bitmap_ptr;
    off_t d_bitmap_ptr;
    off_t i_blocks_ptr;
    off_t d_blocks_ptr;
} superblock_t;

// Inode structure
struct wfs_inode {
    int num;      /* Inode number */
    mode_t mode;  /* File type and mode */
    uid_t uid;    /* User ID of owner */
    gid_t gid;    /* Group ID of owner */
    off_t size;   /* Total size, in bytes */
    int nlinks;   /* Number of links */
    time_t atim;  /* Time of last access */
    time_t mtim;  /* Time of last modification */
    time_t ctim;  /* Time of last status change */
    off_t blocks[7]; /* Data block pointers (6 direct + 1 indirect) */
};

// Global variables
int raid_mode;
int num_inodes;
int num_data_blocks;
char **disk_images;
int num_disks = 0;
int capacity = 10;

// Function to parse command-line arguments
void parse_arguments(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "r:d:i:b:")) != -1) {
        switch (opt) {
            case 'r':
                raid_mode = atoi(optarg);
                if (raid_mode != 0 && raid_mode != 1) {
                    fprintf(stderr, "Invalid RAID mode. Use 0 (RAID 0) or 1 (RAID 1).\n");
                    exit(1);
                }
                break;
            case 'd':
                if (num_disks >= capacity) {
                    capacity *= 2;
                    disk_images = realloc(disk_images, capacity * sizeof(char *));
                    if (!disk_images) {
                        perror("Failed to allocate memory for disk images");
                        exit(1);
                    }
                }
                disk_images[num_disks++] = strdup(optarg);
                break;
            case 'i':
                num_inodes = atoi(optarg);
                if (num_inodes <= 0) {
                    fprintf(stderr, "Number of inodes must be greater than 0.\n");
                    exit(1);
                }
                break;
            case 'b':
                num_data_blocks = atoi(optarg);
                if (num_data_blocks <= 0) {
                    fprintf(stderr, "Number of data blocks must be greater than 0.\n");
                    exit(1);
                }
                num_data_blocks = (num_data_blocks + 31) & ~31; // Round to the nearest multiple of 32
                break;
            default:
                fprintf(stderr, "Usage: %s -r <raid_mode> -d <disk> -i <inodes> -b <blocks>\n", argv[0]);
                exit(1);
        }
    }
    if (num_disks == 0) {
        fprintf(stderr, "At least one disk must be specified.\n");
        exit(1);
    }
}

// Function to initialize the superblock
void init_superblock(superblock_t *sb) {
    sb->num_inodes = num_inodes;
    sb->num_data_blocks = num_data_blocks;
    sb->i_bitmap_ptr = BLOCK_SIZE;
    sb->d_bitmap_ptr = sb->i_bitmap_ptr + ((num_inodes + 7) / 8 + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;
    sb->i_blocks_ptr = sb->d_bitmap_ptr + ((num_data_blocks + 7) / 8 + BLOCK_SIZE - 1) / BLOCK_SIZE * BLOCK_SIZE;
    sb->d_blocks_ptr = sb->i_blocks_ptr + num_inodes * BLOCK_SIZE;
}

// Function to write zeros to a file
void write_zeros(int fd, size_t size) {
    char buffer[BLOCK_SIZE] = {0};
    for (size_t i = 0; i < size / BLOCK_SIZE; i++) {
        if (write(fd, buffer, BLOCK_SIZE) != BLOCK_SIZE) {
            perror("Failed to write zeros to disk");
            exit(1);
        }
    }
}

// Main function
int main(int argc, char *argv[]) {
    disk_images = malloc(capacity * sizeof(char *));
    if (!disk_images) {
        perror("Failed to allocate memory for disk images");
        exit(1);
    }

    parse_arguments(argc, argv);

    // Open all disks
    int disk_fds[MAX_DISKS];
    for (int i = 0; i < num_disks; i++) {
        disk_fds[i] = open(disk_images[i], O_RDWR | O_CREAT, 0666);
        if (disk_fds[i] < 0) {
            perror("Failed to open disk");
            exit(1);
        }
    }

    // Validate disk size
    for (int i = 0; i < num_disks; i++) {
        struct stat st;
        if (fstat(disk_fds[i], &st) < 0 || st.st_size < (num_data_blocks + num_inodes) * BLOCK_SIZE) {
            fprintf(stderr, "Disk %s is too small\n", disk_images[i]);
            exit(1);
        }
    }

    // Initialize superblock
    superblock_t sb;
    init_superblock(&sb);

    // Write superblock, bitmaps, and root inode
    for (int i = 0; i < num_disks; i++) {
        if (lseek(disk_fds[i], 0, SEEK_SET) < 0 || write(disk_fds[i], &sb, sizeof(sb)) != sizeof(sb)) {
            perror("Failed to write superblock");
            exit(1);
        }

        // Write inode and data bitmaps
        lseek(disk_fds[i], sb.i_bitmap_ptr, SEEK_SET);
        write_zeros(disk_fds[i], (num_inodes + 7) / 8);

        lseek(disk_fds[i], sb.d_bitmap_ptr, SEEK_SET);
        write_zeros(disk_fds[i], (num_data_blocks + 7) / 8);

        // Write root inode
        struct wfs_inode root_inode = {
            .num = 0,
            .mode = S_IFDIR | 0755,
            .uid = getuid(),
            .gid = getgid(),
            .size = 0,
            .nlinks = 2,
            .atim = time(NULL),
            .mtim = time(NULL),
            .ctim = time(NULL)
        };
        lseek(disk_fds[i], sb.i_blocks_ptr, SEEK_SET);
        if (write(disk_fds[i], &root_inode, sizeof(root_inode)) != sizeof(root_inode)) {
            perror("Failed to write root inode");
            exit(1);
        }
    }

    // Close disks
    for (int i = 0; i < num_disks; i++) {
        close(disk_fds[i]);
    }

    // Free allocated memory
    for (int i = 0; i < num_disks; i++) {
        free(disk_images[i]);
    }
    free(disk_images);

    return 0;
}
