#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include "wfs.h"

#include <libgen.h>

void *mapped_region;
struct wfs_sb *superblock;

static struct fuse_operations ops = {
    .getattr = my_getattr,
    .mknod   = my_mknod,
    .mkdir   = my_mkdir,
    .unlink  = my_unlink,
    .rmdir   = my_rmdir,
    .read    = my_read,
    .write   = my_write,
    .readdir = my_readdir,
};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <disk1> <disk2> ... <mount_point> [FUSE options]\n", argv[0]);
        return 1;
    }

    // Identify disk image arguments
    int num_disks = 0;
    while (num_disks + 1 < argc && argv[num_disks + 1][0] != '-') {
        num_disks++;
    }

    if (num_disks < 2) {
        fprintf(stderr, "Error: At least two disk images are required.\n");
        return 1;
    }

    // Debugging: Print the disk image names
    printf("Disk images:\n");
    for (int i = 0; i < num_disks; i++) {
        printf("  Disk %d: %s\n", i + 1, argv[i + 1]);
    }

    // Open and map disk images
    void **mapped_regions = malloc(num_disks * sizeof(void *));
    if (!mapped_regions) {
        perror("Error allocating memory for disk mappings");
        return 1;
    }

    struct stat tmp;
    int fd;

    for (int i = 0; i < num_disks; i++) {
        if (access(argv[i + 1], F_OK) != 0) {
            fprintf(stderr, "Error: Disk image '%s' does not exist.\n", argv[i + 1]);
            free(mapped_regions);
            return 1;
        }

        fd = open(argv[i + 1], O_RDWR);
        if (fd < 0) {
            perror("Error opening disk image");
            free(mapped_regions);
            return 1;
        }

        if (fstat(fd, &tmp) < 0) {
            perror("Error getting file stats");
            close(fd);
            free(mapped_regions);
            return 1;
        }

        mapped_regions[i] = mmap(NULL, tmp.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapped_regions[i] == MAP_FAILED) {
            perror("Error mapping memory");
            close(fd);
            free(mapped_regions);
            return 1;
        }
        close(fd);
    }

    // Assume the superblock is mirrored across all disks and read from the first disk
    superblock = (struct wfs_sb *)mapped_regions[0];

    // Debugging: Print updated argc and argv
    printf("Updated argc: %d\n", argc - num_disks);
    printf("Updated argv:\n");
    for (int i = num_disks + 1; i < argc; i++) {
        printf("  argv[%d]: %s\n", i - num_disks, argv[i]);
    }

    // Pass remaining arguments (mount point and FUSE options) to fuse_main
    return fuse_main(argc - num_disks, &argv[num_disks + 1], &ops, NULL);
}
