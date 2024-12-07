// #define FUSE_USE_VERSION 30
// #include <fuse.h>
// #include <stdio.h>
// #include <string.h>
// #include <errno.h>
// #include <stdlib.h>
// #include <sys/mman.h>
// #include <unistd.h>
// #include <stdbool.h>
// #include "wfs.h"

// void **mapped_regions;
// int *fds;
// int rc;

// // struct wfs_sb *superblock;


// struct wfs_inode *find_inode_from_num (int num) {

//     printf("find inode from num starting\n");

//     struct wfs_sb *superblock = (struct wfs_sb *) mapped_regions[0];
//     printf("checkpoint 1\n");
//     int bits = 32;

//     // Access the inode bitmap directly using the offset
//     uint32_t *inode_bitmap = (uint32_t *)((char *) mapped_regions[0] + superblock->i_bitmap_ptr);

//     printf("checkpoint 2\n");

//     int bit_idx = num % bits;
//     int array_idx = num / bits;

//     printf("checkpoint 3\n");

//     if (!(inode_bitmap[array_idx] & (0x1 << bit_idx))) {
//         printf("checkpoint 4\n");
//         return NULL;
//     }

//     printf("checkpoint 5\n");

//     char *inode_table = ((char *) mapped_regions[0]) + superblock->i_blocks_ptr;
//     printf("checkpoint 6\n");
//     printf("find inode from num finished\n");
//     return (struct wfs_inode *)((num * BLOCK_SIZE) + inode_table);
// }


// off_t allocate_data_block() {
//     struct wfs_sb *temp = (struct wfs_sb *) mapped_regions[0];

//     // Manually resolve `mmap_ptr` for `d_bitmap_ptr`
//     uint32_t *bitmap = (uint32_t *)((char *)mapped_regions[0] + temp->d_bitmap_ptr);
//     size_t size = temp->num_data_blocks / 32;

//     // Start of `allocate_block` logic
//     off_t num = -1; // Default to -1 if no block is found
//     for (uint32_t i = 0; i < size; i++) {
//         uint32_t bitmap_region = bitmap[i];
//         uint32_t k = 0;
//         while (bitmap_region != UINT32_MAX && k < 32) {
//             if (!((bitmap_region >> k) & 0x1)) {
//                 bitmap[i] = bitmap[i] | (0x1 << k); // Mark the block as allocated
//                 num = i * 32 + k;                  // Calculate block number
//                 break;
//             }
//             k++;
//         }
//         if (num >= 0) {
//             break; // Exit loop once a free block is found
//         }
//     }
//     // End of `allocate_block` logic

//     if (num < 0) {
//         rc = -ENOSPC;
//         return -1;
//     }

//     // Manually resolve `mmap_ptr` for `d_blocks_ptr` and return offset
//     return temp->d_blocks_ptr + BLOCK_SIZE * num;
// }


// // struct wfs_inode *allocate_inode() {
// //     struct wfs_sb *temp = (struct wfs_sb *) mapped_regions[0];

// //     // Manually resolve `mmap_ptr` for `i_bitmap_ptr`
// //     uint32_t *bitmap = (uint32_t *)((char *) mapped_regions[0] + temp->i_bitmap_ptr);
// //     size_t size = temp->num_inodes / 32;

// //     // Start of `allocate_block` logic
// //     off_t num = -1; // Default to -1 if no block is found
// //     for (uint32_t i = 0; i < size; i++) {
// //         uint32_t bitmap_region = bitmap[i];
// //         uint32_t k = 0;
// //         while (bitmap_region != UINT32_MAX && k < 32) {
// //             if (!((bitmap_region >> k) & 0x1)) {
// //                 bitmap[i] = bitmap[i] | (0x1 << k); // Mark the block as allocated
// //                 num = i * 32 + k;                  // Calculate block number
// //                 break;
// //             }
// //             k++;
// //         }
// //         if (num >= 0) {
// //             break; // Exit loop once a free block is found
// //         }
// //     }
// //     // End of `allocate_block` logic

// //     if (num < 0) {
// //         rc = -ENOSPC; // No space left
// //         return NULL;
// //     }

// //     // Manually resolve `mmap_ptr` for `i_blocks_ptr` and allocate inode
// //     struct wfs_inode *inode = (struct wfs_inode *)((char *) mapped_regions[0] + temp->i_blocks_ptr + num * BLOCK_SIZE);
// //     inode->num = num; // Set the inode number
// //     return inode;
// // }




// struct wfs_inode *locate_inode (const char* path) {

//     printf("locate inode starting\n");

//     printf("this is the path: %s\n", path);

//     // Start with the root inode
//     struct wfs_inode *curr_inode = find_inode_from_num(0);

//     if (strcmp(path, "/") == 0) {
//         return curr_inode;
//     }

//     char *temp_path = strdup(path);
//     if (!temp_path) {
//         return NULL;
//     }

//     // Tokenize the path
//     char *token = strtok(temp_path, "/");

//     // printf("this is what the token is in the beginning (trying to see if it's null or something): %s", token);

//     if (!token) {
//         free(temp_path);
//         return NULL;
//     }
    
//     // check if root node is inode
//     if (!curr_inode) {
//         free(temp_path);
//         return NULL;
//     }

//     // check if curr inode is directory
//     if (!(curr_inode->mode & S_IFDIR)) {
//         free(temp_path);
//         return NULL;
//     }

//     // Traverse tokens
//     while (token) {
//         bool found = false;

//         // Iterate through directory blocks of the current inode
//         for (int i = 0; i < D_BLOCK; i++) {

//             // doesn't have blocks to check
//             if (curr_inode->blocks[i] == 0) {
//                 printf("in locate_inode, we are SKIPPING\n");
//                 continue;
//             } 

//             struct wfs_sb *superblock = (struct wfs_sb *) mapped_regions[0];
//             // first get the mapped region offset, then the block we are on, then the offset within the block
//             char* location = (char *) mapped_regions[0] + superblock->d_blocks_ptr + (curr_inode->blocks[i] * BLOCK_SIZE);

//             // Read directory entries from the block
//             struct wfs_dentry *dentry= (struct wfs_dentry *)(location);

//             for (size_t j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++) {

//                 printf("this is what is being compared: %s and %s\n", dentry[j].name, token);


//                 if (strcmp(dentry[j].name, token) == 0) {                    
//                     // this is now the "root"
//                     // do this process again from the while loop with this as root now

//                     printf("in locate_inode, just called find_inode_from_num\n");
//                     curr_inode = find_inode_from_num(dentry[j].num);
//                     printf("in locate_inode, just finished find_inode_from_num\n");

//                     found = true;
//                     break;
//                 }
//                 // else {
//                 //     printf("This is the difference between the things:")
//                 // }
//             }
//             if (found) {
//                 break; // Exit directory block loop
//             }
//         }

//         if (!found) {
//             // Path component not found
//             printf("we did not find the thing for some reason \n");
//             free(temp_path);
//             return NULL;
//         }
//         else {
//             // Get the next token
//             token = strtok(NULL, "/");
//         }
//     }

//     printf("locate inode finished successfully\n");

//     free(temp_path);
//     return curr_inode;
// }


// size_t allocate_block(uint32_t *bitmap, size_t size) {
//     for (uint32_t i = 0; i < size; i++) {
//         uint32_t bitmap_region = bitmap[i];
//         uint32_t k = 0;
//         while (bitmap_region != UINT32_MAX && k < 32) {
//             if (!((bitmap_region >> k) & 0x1)) {
//                 bitmap[i] = bitmap[i] | (0x1 << k);
//                 return i * 32 + k;
//             }
//             k++;
//         }
//     }
//     return -1;
// }



// int wfs_getattr(const char *path, struct stat *stbuf) {

//     printf("Get attribute starting\n");
//     memset(stbuf, 0, sizeof(struct stat));
//     printf("Memory set\n");

//     if (strcmp(path, "/") == 0) {
//         // Root directory
//         stbuf->st_mode = S_IFDIR | 0755;
//         stbuf->st_nlink = 2;
//         return 0;
//     }

//     // Locate the file or directory in the inode table

//     printf("in my_getattr, just called locate_inode\n");
//     struct wfs_inode *inode = locate_inode(path);
//     printf("in my_getattr, just finished locate_inode\n");
//     if (!inode) {
//         printf("No INODE!\n");
//         return -ENOENT;
//     }
//     printf("Found INODE\n");

//     printf("inode num is %i\n", inode->num);

//     // Populate stat structure
//     stbuf->st_mode = inode->mode;
//     stbuf->st_uid = inode->uid;
//     stbuf->st_gid = inode->gid;
//     stbuf->st_size = inode->size;
//     stbuf->st_nlink = inode->nlinks;
//     stbuf->st_atime = inode->atim;
//     stbuf->st_mtime = inode->mtim;
//     stbuf->st_ctime = inode->ctim;
//     printf("Stat populated\n");

//     return 0;
// }


// struct wfs_inode *allocate_inode() {
//     struct wfs_sb *temp = (struct wfs_sb *) mapped_regions[0];
//     uint32_t *bitmap = (uint32_t *)((char * ) mapped_regions[0] + temp->i_bitmap_ptr); // Use mmap_ptr logic inline
//     size_t size = temp->num_inodes / 32;

//     // Iterate through the bitmap to find and allocate a block
//     for (uint32_t i = 0; i < size; i++) {
//         uint32_t bitmap_region = bitmap[i];
//         uint32_t k = 0;
//         while (bitmap_region != UINT32_MAX && k < 32) {
//             if (!((bitmap_region >> k) & 0x1)) {
//                 // Mark the block as allocated in the bitmap
//                 bitmap[i] = bitmap[i] | (0x1 << k);

//                 // Calculate the allocated block number
//                 off_t num = i * 32 + k;

//                 // Create and initialize the inode structure
//                 struct wfs_inode *inode = (struct wfs_inode *)((char *) mapped_regions[0] + temp->i_blocks_ptr + num * BLOCK_SIZE); // Use mmap_ptr logic inline
//                 inode->num = num;
//                 return inode;
//             }
//             k++;
//         }
//     }

//     // If no free block is found, return an error
//     rc = -ENOSPC;
//     return NULL;
// }




// int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
//     (void)dev;
//     printf("Starting mkdir\n");
//     struct wfs_sb *superblock = (struct wfs_sb *) mapped_regions[0];
//     struct wfs_inode *inode_table = (struct wfs_inode *)((char *)mapped_regions[0] + superblock->i_blocks_ptr);
//     size_t num_inodes = superblock->num_inodes;
//     printf("Superblock set, table set, num_inodes set\n");

//     // Find a free inode
//     struct wfs_inode *new_inode = NULL;
//     for (size_t i = 0; i < num_inodes; i++) {
//       printf("inode: %zd \n", i);
//       if (inode_table[i].nlinks == 0) {
//         new_inode = &inode_table[i];
//         break;
//       }
//     }

//     if (!new_inode) {
//       printf("No INODE\n");
//       return -ENOSPC;
//     }

//     // Initialize the new inode
//     memset(new_inode, 0, sizeof(struct wfs_inode));
//     new_inode->mode = S_IFREG | mode;
//     new_inode->uid = getuid();
//     new_inode->gid = getgid();
//     new_inode->nlinks = 2;
//     new_inode->atim = time(NULL);
//     new_inode->mtim = time(NULL);
//     new_inode->ctim = time(NULL);

//     // Find a free directory entry
//     struct wfs_inode *parent_inode = locate_inode("/"); // Assuming root for simplicity
//     if (!parent_inode) {
//       printf("Root INODE not found");
//       return -ENOENT;
//     }

//     struct wfs_dentry *dentry_table = (struct wfs_dentry *)((char *)mapped_regions[0] + parent_inode->blocks[0]);
//     for (size_t i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
//       printf("dentry: %zd \n", i);
//       if (dentry_table[i].num == 0) {
//         printf("Creating new directory");
//         strncpy(dentry_table[i].name, path, MAX_NAME);
//         dentry_table[i].num = new_inode - inode_table; // Index of the new inode
//         parent_inode->nlinks++;
//         return 0;
//       }
//     }

//     return -ENOSPC;
// }

// int wfs_mkdir(const char *path, mode_t mode) {
    
//     printf("Starting mkdir\n");
//     struct wfs_sb *superblock = (struct wfs_sb *) mapped_regions[0];
//     struct wfs_inode *inode_table = (struct wfs_inode *)((char *)mapped_regions[0] + superblock->i_blocks_ptr);
//     // size_t num_inodes = superblock->num_inodes;
//     printf("Superblock set, table set, num_inodes set\n");

//     // Find a free inode
//     struct wfs_inode *new_inode = allocate_inode();
//     for (size_t i = 0; i < num_inodes; i++) {
//       printf("inode: %zd \n", i);
//       if (inode_table[i].nlinks == 0) {
//         new_inode = &inode_table[i];
//         break;
//       }
//     }

//     if (!new_inode) {
//       printf("No INODE\n");
//       return -ENOSPC;
//     }

//     // Initialize the new inode
//     memset(new_inode, 0, sizeof(struct wfs_inode));
//     new_inode->mode = S_IFDIR | mode;
//     new_inode->uid = getuid();
//     new_inode->gid = getgid();
//     new_inode->nlinks = 2;
//     new_inode->atim = time(NULL);
//     new_inode->mtim = time(NULL);
//     new_inode->ctim = time(NULL);

//     // Find a free directory entry
//     struct wfs_inode *parent_inode = locate_inode("/"); // Assuming root for simplicity
//     if (!parent_inode) {
//       printf("Root INODE not found");
//       return -ENOENT;
//     }

//     struct wfs_dentry *dentry_table = (struct wfs_dentry *)((char *)mapped_regions[0] + parent_inode->blocks[0]);
//     for (size_t i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
//       printf("dentry: %zd \n", i);
//       if (dentry_table[i].num == 0) {
//         printf("Creating new directory");
//         strncpy(dentry_table[i].name, path, MAX_NAME);
//         dentry_table[i].num = new_inode - inode_table; // Index of the new inode
//         parent_inode->nlinks++;
//         return 0;
//       }
//     }

//     return -ENOSPC;

//     // printf("hai3\n");
//     // return 0;
// }

// int wfs_unlink() {
//     printf("hello4\n");
//     return 0;
// }

// int wfs_rmdir() {
//     printf("hello5\n");
//     return 0;
// }

// int wfs_read() {
//     printf("hello6\n");
//     return 0;
// }

// int wfs_write() {
//     printf("hello7\n");
//     return 0;
// }

// int wfs_readdir() {
//     printf("hello8\n");
//     return 0;
// }


// static struct fuse_operations ops = {
//     .getattr = wfs_getattr,
//     .mknod   = wfs_mknod,
//     .mkdir   = wfs_mkdir,
//     .unlink  = wfs_unlink,
//     .rmdir   = wfs_rmdir,
//     .read    = wfs_read,
//     .write   = wfs_write,
//     .readdir = wfs_readdir,
// };



// int main(int argc, char *argv[]) {

//     if (argc < 3) {
//         return -1;
//     }

//   // Identify disk image arguments
//   int num_disks = 0;
//   while (num_disks + 1 < argc && argv[num_disks + 1][0] != '-') {
//     num_disks++;
//   }

//   mapped_regions = malloc(num_disks * sizeof(void *));
//   fds = malloc(num_disks * sizeof(int));

//     for(int i=0; i<num_disks; i++) {
//         fds[i] = open(argv[i + 1], O_RDWR);
//         printf("fds[i] == %i\n", fds[i]);

//         struct stat temp;
//         if (fstat(fds[i], &temp) != 0) {
//             printf(" this si whats wrong fds[%i] == %i\n", i, fds[i]);
//             perror("Error getting file stats this is our own error");
//             exit(1);
//         }

//         mapped_regions[i] = mmap(NULL, temp.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fds[i], 0);
//         if (mapped_regions[i] == MAP_FAILED) {
//             printf("this failed SDFJDAKLSFJDASLKFJ\n");
//             exit(EXIT_FAILURE);
//         }
//     }

//   // Debug: Print disk images
//   printf("Disk images:\n");
//   for (int i = 0; i < num_disks; i++) {
//     printf("  Disk %d: %s\n", i + 1, argv[i + 1]);
//   }

//   // Create a new array for FUSE arguments
//   int fuse_argc = argc - num_disks;
//   char **fuse_argv = malloc(fuse_argc * sizeof(char *));
//   if (!fuse_argv) {
//     // perror("Error allocating memory for FUSE arguments");
//     return -1;
//   }

//   // Copy FUSE-related arguments to fuse_argv (skip argv[0] and disk image paths)
//   fuse_argv[0] = argv[0];
//   for (int i = num_disks + 1; i < argc; i++) {
//     fuse_argv[i - num_disks] = argv[i];
//   }

//   // Debug: Print updated argc and argv for FUSE
//   printf("FUSE argc: %d\n", fuse_argc);
//   printf("FUSE argv:\n");
//   for (int i = 0; i < fuse_argc; i++) {
//     printf("  argv[%d]: %s\n", i, fuse_argv[i]);
//   }

//   return fuse_main(fuse_argc, fuse_argv, &ops, NULL);
// }


#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "wfs.h"
#include <libgen.h>

int num_disks;
int raid_mode;
int* fileDescs;
//char *disks[10];
void **disk_images;
struct wfs_sb* superblock;

size_t allocate_block(uint32_t *bitmap, size_t size) {
  for (uint32_t i = 0; i < size; i++) {
    uint32_t bitmap_region = bitmap[i];
    uint32_t k = 0;
    while (bitmap_region != UINT32_MAX && k < 32) {
      if (!((bitmap_region >> k) & 0x1)) {
        bitmap[i] = bitmap[i] | (0x1 << k);
        return i * 32 + k;
      }
      k++;
    }
  }
  return -1;
}

off_t allocate_DB() {
  struct wfs_sb *superblock = (struct wfs_sb *)disk_images[0];
  off_t num = allocate_block((uint32_t *)((char *)disk_images[0] + superblock->d_bitmap_ptr), superblock->num_data_blocks / 32);
  if (num < 0) {
    return -1;
  }
  return superblock->d_blocks_ptr + BLOCK_SIZE * num;
}

struct wfs_inode *allocate_inode() {
  struct wfs_sb *superblock = (struct wfs_sb *)disk_images[0];
  off_t num = allocate_block((uint32_t *)((char *)disk_images[0] + superblock->d_bitmap_ptr), superblock->num_inodes / 32);
  if (num < 0) {
    return NULL;
  }
  struct wfs_inode *inode = (struct wfs_inode *)(((char *)disk_images[0] + superblock->d_bitmap_ptr) + num * BLOCK_SIZE);
  inode->num = num;
  return inode;
}

struct wfs_inode *locate_inode(const char *path) {

    printf("locate_inode starting\n");

  if (strcmp("/", path) == 0) {
    printf("Found root path\n");
    struct wfs_sb *superblock = (struct wfs_sb *)disk_images[0];
    struct wfs_inode *inode_table = (struct wfs_inode *)((char *)disk_images[0] + superblock->i_blocks_ptr);
    return &inode_table[0]; // Return the root inode
  }

  printf("Inode Starting\n");
  printf("Path: %s\n", path);
  struct wfs_sb *superblock = (struct wfs_sb *)disk_images[0];
  struct wfs_inode *inode_table = (struct wfs_inode *)((char *)disk_images[0] + superblock->i_blocks_ptr);
  printf("Superblock and inode table initialized.\n");

  char path_copy[strlen(path) + 1];
  strcpy(path_copy, path);

  char *token = strtok(path_copy, "/");
  struct wfs_inode *current_inode = &inode_table[0]; // Start at the root inode

  while (token != NULL) {
      int found = 0;
      struct wfs_dentry *dentry_table = (struct wfs_dentry *)((char *)disk_images[0] + current_inode->blocks[0]);


        // the THING IS NEVER FOUND FOR SOME REASON

      for (size_t i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
          if (dentry_table[i].num > 0 && strcmp(dentry_table[i].name, token) == 0) {
              current_inode = &inode_table[dentry_table[i].num];
              found = 1;
              break; // Stop searching once the directory entry is found
          }
          else {
            printf("the difference between the dentry_table[%li].name (%s) and token (%s) is %i \n",
             i, dentry_table[i].name, token, strcmp(dentry_table[i].name, token));
          }
      }

      if (!found) {
          printf("Path component '%s' not found.\n", token);
          return NULL; // Stop if the current token is not found
      }

      token = strtok(NULL, "/"); // Move to the next component in the path
  }

  printf("Inode found for path.\n");
  return current_inode;
}

struct wfs_inode *find_inode_from_num (int num) {

    printf("find inode from num starting\n");

    struct wfs_sb *superblock = (struct wfs_sb *) disk_images[0];
    printf("checkpoint 1\n");
    int bits = 32;

    // Access the inode bitmap directly using the offset
    uint32_t *inode_bitmap = (uint32_t *)((char *) disk_images[0] + superblock->i_bitmap_ptr);

    printf("checkpoint 2\n");

    int bit_idx = num % bits;
    int array_idx = num / bits;

    printf("checkpoint 3\n");

    if (!(inode_bitmap[array_idx] & (0x1 << bit_idx))) {
        printf("checkpoint 4\n");
        return NULL;
    }

    printf("checkpoint 5\n");

    char *inode_table = ((char *) disk_images[0]) + superblock->i_blocks_ptr;
    printf("checkpoint 6\n");
    printf("find inode from num finished\n");
    return (struct wfs_inode *)((num * BLOCK_SIZE) + inode_table);
}

static int wfs_getattr(const char *path, struct stat *stbuf) {
  printf("Get attribute starting\n");
  printf("Path: %s\n", path);
  memset(stbuf, 0, sizeof(struct stat));
  printf("Memory set\n");

  if (strcmp(path, "/") == 0) {
    // Root directory
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 2;
    return 0;
  }

  // Locate the file or directory in the inode table
  struct wfs_inode *inode = locate_inode(path);
  if (!inode) {
    printf("No INODE!\n");
    return -ENOENT;
  }
  else printf("FOUND INODE!\n");

  // Populate stat structure
  stbuf->st_mode = inode->mode;
  stbuf->st_uid = inode->uid;
  stbuf->st_gid = inode->gid;
  stbuf->st_size = inode->size;
  stbuf->st_nlink = inode->nlinks;
  stbuf->st_atime = inode->atim;
  stbuf->st_mtime = inode->mtim;
  stbuf->st_ctime = inode->ctim;
  printf("Stat populated\n");

  return 0;
}

static int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
  printf("Starting mknod\n");
  //struct wfs_sb *superblock = (struct wfs_sb *)disk_images[0];
  //struct wfs_inode *inode_table = (struct wfs_inode *)((char *)disk_images[0] + superblock->i_blocks_ptr);
  //size_t num_inodes = superblock->num_inodes;

  // Find a free inode
  printf("Allocating node\n");
  struct wfs_inode *new_inode = allocate_inode();

  if (!new_inode) {
    printf("No INODE\n");
    return -ENOSPC;
  }

  // Initialize the new inode
  memset(new_inode, 0, sizeof(struct wfs_inode));
  new_inode->mode = S_IFDIR | mode;
  new_inode->uid = getuid();
  new_inode->gid = getgid();
  new_inode->nlinks = 2;
  new_inode->atim = time(NULL);
  new_inode->mtim = time(NULL);
  new_inode->ctim = time(NULL);
  new_inode->num = 12;

  // Find a free directory entry
  struct wfs_inode *parent_inode = locate_inode("/");
  if (!parent_inode) {
    printf("Root INODE not found");
    return -ENOENT;
  }

  struct wfs_dentry *dentry_table = (struct wfs_dentry *)((char *)disk_images[0] + parent_inode->blocks[0]);
  for (size_t i = 0; i < BLOCK_SIZE / sizeof(struct wfs_dentry); i++) {
    printf("dentry: %zd \n", i);
    if (dentry_table[i].num == 0) {
      printf("Creating new directory");
      strncpy(dentry_table[i].name, path, MAX_NAME);
      dentry_table[i].num = new_inode->num;
      parent_inode->nlinks++;
      return 0;
    }
  }

  return -ENOSPC;
}

static int wfs_mkdir(const char *path, mode_t mode) {


    // NOTICE: take this out
    // struct wfs_inode* check = locate_inode(path);
    // if (!check) return -EEXIST;


    printf("Starting mkdir\n");
    struct wfs_sb *superblock = (struct wfs_sb *)disk_images[0];
    struct wfs_inode *inode_table = (struct wfs_inode *)((char *)disk_images[0] + superblock->i_blocks_ptr);

//     struct wfs_sb {
//     size_t num_inodes;
//     size_t num_data_blocks;
//     off_t i_bitmap_ptr;
//     off_t d_bitmap_ptr;
//     off_t i_blocks_ptr;
//     off_t d_blocks_ptr;
//     // Extend after this line
    
//     int num_disks;
//     int raid_mode;
// };


    printf("data fields of superblock\n");
    printf("num inodes is %li\n", superblock->num_inodes);
    printf("num data blocks is %li\n", superblock->num_data_blocks);
    printf("i_bitmap_ptr is %li\n", superblock->i_bitmap_ptr);
    printf("d_bitmap_ptr is %li\n", superblock->d_bitmap_ptr);
    printf("i_blocks_ptr is %li\n", superblock->i_blocks_ptr);
    printf("d_blocks_ptr is %li\n", superblock->d_blocks_ptr);


    size_t num_inodes = superblock->num_inodes;
    printf("Superblock set, table set, num_inodes set\n");
    
    char path_copy[MAX_NAME], dir_copy[MAX_NAME];
    strncpy(path_copy, path, MAX_NAME-1);
    strncpy(dir_copy, path, MAX_NAME-1);
    path_copy[MAX_NAME-1] = dir_copy[MAX_NAME-1] = '\0';
    
    char *dir_name = basename(dir_copy);
    printf("the dir name is %s\n", dir_name);

    char *parent_path = dirname(path_copy);
    printf("the parent path is %s\n", parent_path);
    
    struct wfs_inode* parent_inode_num = locate_inode(parent_path);
    if (!parent_inode_num) {
        printf("we are in here sadklfja;lf");
        return -ENOENT; // Parent directory not found
    }

    int new_inode_num;
    for (size_t i = 0; i < num_inodes; i++) {
      printf("inode: %zd \n", i);

      // NOTICE: PRETTY SURE THIS CAUSES A SEGFAULT, HARDCODING IT TO TEST IT OUT
    //   if (inode_table[i].nlinks == 0) {
    //     new_inode_num = i;
    //     break;
    //   }

        printf("checkpoint in mkdir %li\n", i);

         if (inode_table[i].nlinks == 0) {
            new_inode_num = 20;
            break;
        }
    }

    printf("checkpoint in mkdir abc\n");

    // int new_inode_num = get_free_inode();
    if (new_inode_num < 0) return -ENOSPC;
    
    struct wfs_inode new_dir = {0};
    printf("checkpoint in mkdir 1\n");
    new_dir.num = new_inode_num;
    printf("checkpoint in mkdir 2\n");
    new_dir.mode = S_IFDIR | (mode & 0777);
    printf("checkpoint in mkdir 3\n");
    new_dir.uid = getuid();
    printf("checkpoint in mkdir 4\n");
    new_dir.gid = getgid();
    printf("checkpoint in mkdir 5\n");
    new_dir.nlinks = 2;
    new_dir.atim = new_dir.mtim = new_dir.ctim = time(NULL);
    printf("checkpoint in mkdir 6\n");
    
    struct wfs_inode parent = {0};
    printf("checkpoint in mkdir 7\n");
    memcpy(&parent, parent_inode_num, sizeof(struct wfs_inode));
    printf("checkpoint in mkdir 8\n");
    
    if (parent.blocks[0] == 0 && parent.size == 0) {
        printf("just before allocate_db in mkdir\n");
        int block_num = allocate_DB();
        printf("just after allocate_db in mkdir\n");
        // if (block_num < 0) {
        //     free_inode(new_inode_num);
        //     return -ENOSPC;
        // }
        parent.blocks[0] = block_num;
        

        char zero_block[BLOCK_SIZE] = {0};

        // printf("disk_images[0] = %")

        printf("before this for loop in mkdir\n");
        for (int disk = 0; disk < num_disks; disk++) {
            printf("iteration %i in this for loop in in mkdir\n", disk);

            if (disk_images[disk] == NULL) {
                printf("disk_images[%d] is NULL\n", disk);
            } else {
                printf("disk_images[%d] is valid, address: %p\n", disk, disk_images[disk]);
            }

            printf("disk_images[disk] is %p\n", disk_images[disk]);

            memcpy((char*)disk_images[disk] + superblock->d_blocks_ptr + block_num * BLOCK_SIZE,
                   zero_block, BLOCK_SIZE);

            printf("sjdfldsjfdsakjf;ldksaf\n");
        }
    }

    printf("after if statement in mkdir\n");

    struct wfs_dentry entry = {0};
    //   = "sdfkdjsalfd";
    strncpy(entry.name, dir_name, MAX_NAME - 1);
    entry.num = new_inode_num;

    parent.size += sizeof(struct wfs_dentry);
    parent.nlinks++;

    for (int disk = 0; disk < num_disks; disk++) {
        memcpy((char*)disk_images[disk] + superblock->i_blocks_ptr + new_inode_num * BLOCK_SIZE,
               &new_dir, sizeof(struct wfs_inode));
               
        memcpy((char*)disk_images[disk] + superblock->i_blocks_ptr + parent_inode_num->num * BLOCK_SIZE,
               &parent, sizeof(struct wfs_inode));
               
        memcpy((char*)disk_images[disk] + superblock->d_blocks_ptr + parent.blocks[0] * BLOCK_SIZE + parent.size - sizeof(struct wfs_dentry),
               &entry, sizeof(struct wfs_dentry));
               
        msync(disk_images[disk], superblock->i_blocks_ptr + (new_inode_num + 1) * BLOCK_SIZE, MS_SYNC);
        msync(disk_images[disk], superblock->d_blocks_ptr + (parent.blocks[0] + 1) * BLOCK_SIZE, MS_SYNC);
    }
    
    return 0;
}

static int wfs_unlink(const char *path) {
  printf("unlink called for path: %s\n", path);
  return 0;
}

static int wfs_rmdir(const char *path) {
  printf("rmdir called for path: %s\n", path);
  return 0;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  printf("read called for path: %s\n", path);
  return 0;
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  printf("write called for path: %s\n", path);
  return size;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  printf("readdir called for path: %s\n", path);
  return 0;
}

static struct fuse_operations ops = {
  .getattr = wfs_getattr,
  .mknod   = wfs_mknod,
  .mkdir   = wfs_mkdir,
  .unlink  = wfs_unlink,
  .rmdir   = wfs_rmdir,
  .read    = wfs_read,
  .write   = wfs_write,
  .readdir = wfs_readdir,
};

int main(int argc, char *argv[]) {
  num_disks = 0;
  while (num_disks + 1 < argc && access(argv[num_disks + 1], F_OK) == 0)
  {
    num_disks++;
  }
  
  if (num_disks < 1) {
    fprintf(stderr, "Need at least 1 disks\n");
    return -1;
  }

  disk_images = malloc(sizeof(void *) * num_disks);
  if (disk_images == NULL) {
    fprintf(stderr, "Memory allocation failed for disks\n");
    return -1;
  }

  fileDescs = malloc(sizeof(int) * num_disks);
  if (fileDescs == NULL) {
    fprintf(stderr, "Memory allocation failed for fileDescs\n");
    return -1;
  }

  struct stat st;
  for (int i = 0; i < num_disks; i++) {
    fileDescs[i] = open(argv[i + 1], O_RDWR);
    if (fileDescs[i] == -1) {
      fprintf(stderr, "Failed to open disk %s\n", argv[i + 1]);
      return -1;
    }

    if (fstat(fileDescs[i], &st) != 0) {
      fprintf(stderr, "Failed to get disk size for %s\n", argv[i + 1]);
      return -1;
    }
    //diskSize = st.st_size;

    printf("num_disks is %i\n", num_disks);

    disk_images[i] = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fileDescs[i], 0);
    if (disk_images[i] == MAP_FAILED) {
      fprintf(stderr, "Failed to mmap disk %s\n", argv[i + 1]);
      return -1;
    }
  }

  superblock = (struct wfs_sb *)disk_images[0];

  if (superblock == NULL) {
    fprintf(stderr, "Failed to access superblock\n");
    return -1;
  }

  //num_disks = superblock->num_disks;
  //raid_mode = superblock->mode;

  int f_argc = argc - num_disks;
  char **f_argv = argv + num_disks;

  printf("f_argc: %d\n", f_argc);
  for (int i = 0; i < f_argc; i++) {
    printf("f_argv[%d]: %s\n", i, f_argv[i]);
  }

  int rc = fuse_main(f_argc, f_argv, &ops, NULL);
  printf("Returned from fuse\n");

  for (int i = 0; i < num_disks; i++) {
    if (munmap(disk_images[i], st.st_size) != 0) {
      fprintf(stderr, "Failed to unmap disk %d\n", i);
      return -1;
    }
    close(fileDescs[i]);
  }

  free(disk_images);
  free(fileDescs);

  return rc;
}
