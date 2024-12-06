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

void *mapped_region;
struct wfs_sb *superblock;

int my_getattr() {
    printf("hello\n");
    return -1;
}

int my_mknod() {
    return -1;
}

int my_mkdir() {
    printf("hai\n");
    return -1;
}

int my_unlink() {
    return -1;
}

int my_rmdir() {
    return -1;
}

int my_read() {
    return -1;
}

int my_write() {
    return -1;
}

int my_readdir() {
    return -1;
}


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
        return -1;
    }

    fprintf("ARGV ORIGINALLY");
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d]: %s\n", i, argv[i]);
    }

    // Identify disk image arguments
    int num_disks = 0;
    while (num_disks + 1 < argc && argv[num_disks + 1][0] != '-') {
        num_disks++;
    }

    if (num_disks < 2) {
        fprintf(stderr, "Error: At least two disk images are required.\n");
        return -1;
    }

    // Debug: Print disk images
    printf("Disk images:\n");
    for (int i = 0; i < num_disks; i++) {
        printf("  Disk %d: %s\n", i + 1, argv[i + 1]);
    }

    // Adjust argc and argv for FUSE
    int fuse_argc = argc - num_disks;
    char **fuse_argv = &argv[num_disks + 1];

    // Debug: Print updated argc and argv for FUSE
    printf("FUSE argc: %d\n", fuse_argc);
    printf("FUSE argv:\n");
    for (int i = 0; i < fuse_argc; i++) {
        printf("  argv[%d]: %s\n", i, fuse_argv[i]);
    }

    // Start FUSE
    return fuse_main(fuse_argc, fuse_argv, &ops, NULL);
}