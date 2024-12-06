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
// struct wfs_sb *superblock;


struct wfs_inode *find_inode_from_num (int num) {

    struct wfs_sb *superblock = (struct wfs_sb *) mapped_region;
    int bits = 32;

    // Access the inode bitmap directly using the offset
    uint32_t *inode_bitmap = (uint32_t *)((char *) mapped_region + superblock->i_bitmap_ptr);

    int bit_idx = num % bits;
    int array_idx = num / bits;

    if (!(inode_bitmap[array_idx] & (0x1 << bit_idx))) {
        return -1;
    }

    char *inode_table = ((char *) mapped_region) + superblock->i_blocks_ptr;
    return (struct wfs_inode *)((num * BLOCK_SIZE) + inode_table);
}



struct wfs_inode *locate_inode (char* path) {

    if (strcmp(path, "/") == 0) {
        return 0;
    }

    char *temp_path = strdup(path);
    if (!temp_path) {
        return -1;
    }

    // Tokenize the path
    char *token = strtok(temp_path, "/");
    if (!token) {
        free(temp_path);
        return -1;
    }

    // Start with the root inode
    struct wfs_inode *curr_inode = find_inode_from_num(0);
    
    // check if root node is inode
    if (!curr_inode) {
        free(temp_path);
        return -1;
    }

    // check if curr inode is directory
    if (!(curr_inode->mode & S_IFDIR)) {
        free(temp_path);
        return -1;
    }


    // Traverse tokens
    while (token) {
        bool found = false;

        // Iterate through directory blocks of the current inode
        for (int i = 0; i < D_BLOCK; i++) {

            // doesn't have blocks to check
            if (curr_inode->blocks[i] == 0) {
                continue;
            } 

            // first get the mapped region offset, then the block we are on, then the offset within the block
            char* location = (char *) mapped_region + superblock->d_blocks_ptr + (curr_inode->blocks[i] * BLOCK_SIZE);

            // Read directory entries from the block
            struct wfs_dentry *dentry= (struct wfs_dentry *)(location);

            for (size_t j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {

                if (strcmp(dentry[j].name, token) == 0) {
                    
                    curr_inode = find_inode_from_num(dentry[j].num);
                    found = true;
                    break;
                }
            }
            if (found) {
                break; // Exit directory block loop
            }
        }

        if (!found) {
            // Path component not found
            free(temp_path);
            return -1;
        }
        else {
            // Get the next token
            token = strtok(NULL, "/");
        }
    }

    free(path_copy);
    return curr_inode;
}




int my_getattr() {
    printf("hello1\n");
    return 0;
}

int my_mknod() {
    printf("hello2\n");
    return 0;
}

int my_mkdir() {
    printf("hai3\n");
    return 0;
}

int my_unlink() {
    printf("hello4\n");
    return 0;
}

int my_rmdir() {
    printf("hello5\n");
    return 0;
}

int my_read() {
    printf("hello6\n");
    return 0;
}

int my_write() {
    printf("hello7\n");
    return 0;
}

int my_readdir() {
    printf("hello8\n");
    return 0;
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
    return -1;
  }

  for (int i = 0; i < argc; i++) {
    printf("  argv[%d]: %s\n", i, argv[i]);
  }

  // Identify disk image arguments
  int num_disks = 0;
  while (num_disks + 1 < argc && argv[num_disks + 1][0] != '-') {
    num_disks++;
  }

  // Debug: Print disk images
  printf("Disk images:\n");
  for (int i = 0; i < num_disks; i++) {
    printf("  Disk %d: %s\n", i + 1, argv[i + 1]);
  }

  // Create a new array for FUSE arguments
  int fuse_argc = argc - num_disks; // Exclude disk images and argv[0]
  char **fuse_argv = malloc(fuse_argc * sizeof(char *));
  if (!fuse_argv) {
    perror("Error allocating memory for FUSE arguments");
    return -1;
  }

  // Copy FUSE-related arguments to fuse_argv (skip argv[0] and disk image paths)
  fuse_argv[0] = argv[0];
  for (int i = num_disks + 1; i < argc; i++) {
    fuse_argv[i - num_disks] = argv[i];
  }

  // Debug: Print updated argc and argv for FUSE
  printf("FUSE argc: %d\n", fuse_argc);
  printf("FUSE argv:\n");
  for (int i = 0; i < fuse_argc; i++) {
    printf("  argv[%d]: %s\n", i, fuse_argv[i]);
  }

  // Start FUSE
  int result = fuse_main(fuse_argc, fuse_argv, &ops, NULL);
  printf("Fuse main passed\n");

  // Free allocated memory
  free(fuse_argv);
  printf("Args freed\n");

  return result;
}