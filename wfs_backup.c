/* wfs.c */
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <bits/stat.h>
#include "wfs.h"
#include <fuse.h>
#include <stdbool.h>


struct wfs_sb * superblock;
char * disk_image;
static char *disk_path; // Pointer to the memory-mapped disk image
char *mount_point;




struct wfs_inode * make_new_node(mode_t mode) {

    struct wfs_inode * new_node = malloc(sizeof(struct wfs_inode));

    new_node->num = -1;
    new_node->mode = mode;
    new_node->uid = getuid();
    new_node->gid = getgid();
    new_node->size = 0; // Initially, the size of the file is 0
    new_node->nlinks = 1; // This is the first link to the file
    new_node->atim = time(NULL); // Set access time to current time
    new_node->mtim = time(NULL); // Set modification time to current time
    new_node->ctim = time(NULL); // Set creation time to current time


    int mask = 1;
    int bit = -1;
    void* offset;
    for (int i = 0; i < superblock->num_inodes; i++) {

        bit = disk_image[superblock->i_bitmap_ptr + (i / 8)] & (mask << (i % 8));

        // Inode exists
        if (bit == 0) {
            new_node->num = i; // TODO
            disk_image[superblock->i_bitmap_ptr + (i / 8)] = disk_image[superblock->i_bitmap_ptr + (i / 8)] | (mask << (i % 8));
            printf("bitmap is: %x", disk_image[superblock->i_bitmap_ptr + (i / 8)]);
            offset = (void*)(disk_image + superblock->i_blocks_ptr + BLOCK_SIZE*i);
            // printf("%s", offset);
            memcpy(offset, new_node, sizeof(struct wfs_inode));
            return new_node;
        }       
    }


    return NULL; 
}


void get_parent_directory(const char *path, char *parent_dir) {
    strcpy(parent_dir, path);
    char *last_slash = strrchr(parent_dir, '/');
    if (last_slash != NULL) {
        *last_slash = '\0'; // Null-terminate to get the parent directory
    } else {
        strcpy(parent_dir, "/"); // If there's no slash, parent directory is root
    }
}


int get_inode_num(const char* path) {
    int current_inode_num = 0;

    char path_copy[strlen(path)];
    strcpy(path_copy, path);

    char *token = strtok(path_copy, "/");

    struct wfs_dentry* current_entry ;
        
    if (token == NULL) {
        printf("token is NULL\n");
        return 0;
    }


    printf("bitmap from the other side %x\n", disk_image[superblock->i_bitmap_ptr + (0 / 8)]);
    while (token != NULL) {
        printf("token is %s\n", token);
        int found = 0;
        // printf("token: %s\n", token);
        for (int i = 0; i < superblock->num_inodes; i++) {
            int j = i % 8;

            int bit = (disk_image[superblock->i_bitmap_ptr + (i / 8)]) & (1 << j);

            if (bit != 0) {
                struct wfs_inode* current_inode = (struct wfs_inode*) (disk_image + (superblock->i_blocks_ptr + i * BLOCK_SIZE));
                // printf("%d\n",current_inode->num);
                // printf("inode %d\n", i);
                if (current_inode->num == current_inode_num) {

                    for (int j = 0; j < D_BLOCK; j++) {
                        off_t offset = current_inode->blocks[j];
                        // printf("current offset is %ld\n", offset);

                        if (offset != 0) {
                            printf("offset: %ld\n", offset);
                            printf("disk image: %ld\n", (off_t) disk_image);

                            for (int k = 0; k < 16; k++) {
                            current_entry = (struct wfs_dentry*) (disk_image + offset + k * sizeof(struct wfs_dentry));
                            if(current_entry != NULL) {
                                if (current_entry->num != 0) {
                                    printf("name: %s; num: %d\n", current_entry->name, current_entry->num);
                                }
                                printf("comparing this %s and this %s\n", current_entry->name, token);
                                if (!strcmp(current_entry->name, token)) {
                                    current_inode_num = current_entry->num;
                                    printf("next inode num: %d\n", current_entry->num);
                                    found = 1;
                                    break;

                                }
                            }
                        }
                        }
                    }
                    if (!found) {
                        return -1;
                    } else {
                        break;
                    }   
                }
            }

        }
        token = strtok(NULL, "/");

    }
    // printf("returning inode: %d\n", current_inode_num);

    return current_inode_num;
}



struct wfs_inode * get_inode(int num) {
    struct wfs_inode * curr_node;

    int mask = 1;
    int bit = -1;

    for (int i = 0; i < superblock->num_inodes; i++) {

        bit = disk_image[superblock->i_bitmap_ptr + (i / 8)] & (mask << (i % 8));
        
        if (bit != 0) {
            curr_node = (struct wfs_inode * ) (disk_image + superblock->i_blocks_ptr + (i*BLOCK_SIZE));
            if (curr_node->num == num) {
                return curr_node;
            }
        }       
    }
    return NULL; 
}

int del_inode(int num) {
    struct wfs_inode * curr_node;

    int mask = 1;
    int bit = -1;

    for (int i = 0; i < superblock->num_inodes; i++) {

        bit = disk_image[superblock->i_bitmap_ptr + (i / 8)] & (mask << (i % 8));
        
        if (bit != 0) {
            curr_node = (struct wfs_inode * ) (disk_image + superblock->i_blocks_ptr + (i*BLOCK_SIZE));
            if (curr_node->num == num) {
                disk_image[superblock->i_bitmap_ptr + (i / 8)] = disk_image[superblock->i_bitmap_ptr + (i / 8)] & ~(mask << (i % 8));
                return 0;
            }
        }       
    }
    return -1; 
}

static int wfs_getattr(const char* path, struct stat* stbuf) {
    printf("Entering: getattr\n");

    int num = get_inode_num(path);
    struct wfs_inode * temp_node = get_inode(num);

    if (temp_node == NULL) {
        printf("Leaving: getattr. file %s doesn't exist.\n", path);
        return -ENOENT;
    }
    // printf("node: %d", temp_node->num);

    // // stbuf->st_blocks = node->blocks;

    stbuf->st_mode         = temp_node->mode; 
    stbuf->st_uid          = temp_node->uid;  
    stbuf->st_gid          = temp_node->gid;  
    stbuf->st_size         = temp_node->size; 
    // stbuf->st_nlink        = temp_node->nlinks;
    stbuf->st_atime        = temp_node->atim;
    stbuf->st_mtime        = temp_node->mtim;
    // stbuf->st_ctime        = temp_node->ctim;

    
    printf("Leaving: getattr\n");
    // Implement getattr function
    return 0;
}

void split_path(const char* path, char** directory, char** basename) {
    char* last_slash = strrchr(path, '/'); // Find the last occurrence of '/'
    
    if (last_slash != NULL) {
        // Allocate memory for the directory part
        *directory = malloc((last_slash - path + 1) * sizeof(char));
        if (*directory == NULL) {
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }
        
        // Copy the directory part of the path
        strncpy(*directory, path, last_slash - path);
        (*directory)[last_slash - path] = '\0'; // Null-terminate the directory part
        
        // Allocate memory for the basename part
        *basename = malloc((strlen(last_slash + 1) + 1) * sizeof(char));
        if (*basename == NULL) {
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }
        
        // Copy the basename part of the path
        strcpy(*basename, last_slash + 1);
    } else {
        // If no '/' is found, the entire path is considered as basename
        *directory = NULL;
        *basename = malloc((strlen(path) + 1) * sizeof(char));
        if (*basename == NULL) {
            fprintf(stderr, "Memory allocation failed.\n");
            exit(EXIT_FAILURE);
        }
        strcpy(*basename, path);
    }
}

struct wfs_inode * get_root() {
    struct wfs_inode * root = (struct wfs_inode *) (disk_image + superblock->i_blocks_ptr);
    return root;
}

// off_t get_doffset(struct wfs_dentry * entry) {
//     uint8_t bitmap;
//     for (int i = 0; i < superblock->num_data_blocks; i++) {
//         bitmap = (disk_image[superblock->d_bitmap_ptr + (i / 8)]) & (1 << (i % 8));
//         if (bitmap == 0) {
//             disk_image[superblock->d_bitmap_ptr + (i / 8)] = (disk_image[superblock->d_bitmap_ptr + (i / 8)] | (1 << (i % 8)));
//             return (off_t) (superblock->d_blocks_ptr + (i*BLOCK_SIZE));
//         }
//     }
//     return -1;
// }

off_t set_doffset(struct wfs_dentry * entry, struct wfs_inode * parent) {

    uint8_t bitmap;
    off_t offset;
    int i;
    for (i = 0; i < superblock->num_data_blocks; i++) {
        bitmap = (disk_image[superblock->d_bitmap_ptr + (i / 8)]) & (1 << (i % 8));
        if (bitmap == 0) {
            offset = (superblock->d_blocks_ptr + (i*BLOCK_SIZE));
            break;
        }
    }

    for (int j = 0 ; j < D_BLOCK ; j++) {
        if (parent->blocks[j] == 0) {
            disk_image[superblock->d_bitmap_ptr + (i / 8)] = (disk_image[superblock->d_bitmap_ptr + (i / 8)] | (1 << (i % 8)));
            parent->blocks[j] = offset;
            return offset;
        }
    }
    return -1;
}



struct wfs_dentry * write_entry(const char *path, struct wfs_inode * node) {
    char* directory;
    char* basename;
    split_path(path, &directory, &basename);
    struct wfs_inode * parent_node;
    int parent_num;

    struct wfs_dentry * new_entry = malloc(sizeof(struct wfs_dentry));
    strcpy(new_entry->name, basename);

    if (strlen(directory) < 1) {
        // If directory is empty, it means we are creating an entry in the root directory
        new_entry->num = node->num;
        struct wfs_inode * root = get_root();
        // root->blocks[0] = (off_t) get_doffset(new_entry);
        memcpy((root->blocks[0] + disk_image), new_entry, sizeof(struct wfs_dentry));
    } else {
        // If directory is not empty, we need to find the parent directory
        parent_num = get_inode_num(directory);
        printf("directory is: %s\n", directory);
        if (parent_num == -1) return NULL;
        parent_node = get_inode(parent_num);
        // printf
        
        // Find an available data block for the new entry
        off_t offset = set_doffset(new_entry,parent_node);
        if (offset == -1) return NULL;
        
        // Write the new entry to the parent directory
        memcpy((disk_image + offset), new_entry, sizeof(struct wfs_dentry));
        parent_node->size += sizeof(struct wfs_dentry); // Update parent directory size
    }
    
    return new_entry;
}


// struct wfs_dentry * write_entry(const char *path, struct wfs_inode * node) {
//     char* directory;
//     char* basename;
//     split_path(path, &directory, &basename);
//     struct wfs_inode * parent_node;
//     int parent_num;

//     // int num = get_inode_num(directory);   

//     struct wfs_dentry * new_entry = malloc(sizeof(struct wfs_dentry));
//     strcpy(new_entry->name, basename);

//     if (strlen(directory) < 1) {
//         new_entry->num = node->num;
//         struct wfs_inode * root = get_root();
//         root->blocks[0] = (off_t) get_doffset(new_entry);
//         memcpy((root->blocks[0] + disk_image), new_entry, sizeof(struct wfs_dentry));
//     } else {
//         parent_num = get_inode_num(directory);
//         if (parent_num == -1) return NULL;
//         parent_node = get_inode(parent_num);
//         printf("parent's size %ld\n", parent_node->size);
        

//     }
    
    // printf("addr %lx vs just it %lx, calulated %lx, ptr %lx\n", (off_t) &new_entry, (off_t) new_entry, root->blocks[0], superblock->d_blocks_ptr);

//     return NULL;
// }

static int wfs_mknod(const char *path, mode_t mode, dev_t dev) {
    printf("Entering: mknod\n");

    struct wfs_inode * new_node = make_new_node(mode | S_IFREG);
    if (new_node == NULL) return ENOSPC;
    printf("new node number %d\n", new_node->num);

    write_entry(path, new_node);

    printf("Leaving: mknod\n");
    return 0;
}

static int wfs_mkdir(const char *path, mode_t mode) {
    printf("Entering: mkdir\n");

    struct wfs_inode * new_node = make_new_node(mode| S_IFDIR);
    if (new_node == NULL) return ENOSPC;

    printf("new node number %d\n", new_node->num);

    write_entry(path, new_node);

    printf("Leaving: mkdir\n");
    return 0;
}


static int wfs_unlink(const char *path) {
    printf("Entering: unlink\n");

    int num = get_inode_num(path);
    int result = del_inode(num);



    
    printf("Leaving: unlink\n");
    // Implement unlink function
    return result;
}

static int wfs_rmdir(const char *path) {
    printf("Entering: rmdir\n");


    
    printf("Leaving: rmdir\n");
    // Implement rmdir function
    return 0;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("Entering: read\n");

    char path_copy[strlen(path)];
    strcpy(path_copy,path);

    int num = get_inode_num(path_copy);

    if (num == -1) return -1;

    struct wfs_inode * node = get_inode(num);
    off_t block;

    for (int j = 0; j < D_BLOCK ; j++) {
        block = node->blocks[j];
        if (block != 0) {
            char* file_content = (char*) (disk_image + block + offset);
            memcpy(buf,file_content,size);
            return size;
        }
    }
    printf("Leaving: read\n");

    return -1;
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    printf("Entering: write\n");


    
    printf("Leaving: write\n");
    // Implement write function
    return 0;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("Entering: readdir\n");


    
    printf("Leaving: readdir\n");
    // Implement readdir function
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


static void init_filesystem() {
    int fd = open(disk_path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (fd == -1) {
        perror("Failed to open disk image");
        exit(EXIT_FAILURE);
    }

    // Read superblock from disk
    struct stat st;
    fstat(fd, &st);

    disk_image = mmap(NULL, st.st_size, PROT_EXEC | PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    if (disk_image == MAP_FAILED) {
        perror("Failed to mmap superblock");
        exit(EXIT_FAILURE);
    }

    superblock = (struct wfs_sb *) disk_image;

    // printf("superblock inode: %ld\n",superblock->num_inodes);

    close(fd);
    // printf("bitmap pointer: %ld\n",superblock->d_bitmap_ptr);
    return;
}

int main(int argc, char *argv[]) {
    
    // for (int i = 0; i < argc ; i++){
    //     printf("%s \n", argv[i]);
    // }
    // shmem = mmap(NULL, shm_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    disk_path = argv[1];
    mount_point = argv[argc - 1];
    init_filesystem();
    printf("num nodes: %ld, num blocks %ld \n", superblock->num_inodes, superblock->num_data_blocks);

    // Initialize FUSE with specified operations
    return fuse_main(argc-1, &argv[1], &ops, NULL);
}
