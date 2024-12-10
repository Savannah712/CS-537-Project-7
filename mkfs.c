#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "wfs.h"

#define SUPERBLOCK_SIZE sizeof(struct wfs_sb)
struct wfs_sb * superblock;
char *disk_img;
int num_inodes, num_blocks;

int main(int argc, char *argv[]) {

    fprintf(stdout,"hello\n");
    // Check if correct number of arguments are provided
    if (argc != 7) {
        fprintf(stderr, "Usage: %s -d disk_img -i num_inodes -b num_blocks\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "d:i:b:")) != -1) {
        switch (opt) {
            case 'd':
                disk_img = optarg;
                break;
            case 'i':
                num_inodes = ((atoi(optarg)  + 31) / 32) * 32;
                break;
            case 'b':
                num_blocks = ((atoi(optarg) + 31) / 32) * 32;
                break;
            default:
                fprintf(stderr, "Usage: %s -d disk_img -i num_inodes -b num_blocks\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }


    fprintf(stdout,"disk: %s, inodes: %d, blocks: %d\n", disk_img,num_inodes,num_blocks);

    fprintf(stdout,"size of superblock is %ld\n", sizeof(struct wfs_sb));

    ////////////////////////////////////////////////////////
    //// Make sure we have enough space for all blocks ////
    //////////////////////////////////////////////////////
    // int fd = open(disk_img, O_RDONLY);
    int fd = open(disk_img, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

    if (fd == -1) {
        perror("Error opening disk image file");
        exit(EXIT_FAILURE);
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("Error getting file status");
        close(fd);
        exit(EXIT_FAILURE);
    }

    // close(fd);

    size_t required_size = sizeof(struct wfs_sb) + (num_blocks/8) + (num_inodes/8) + (BLOCK_SIZE * num_blocks) + (BLOCK_SIZE * num_inodes);
    // off_t required_size = (BLOCK_SIZE * num_blocks) + (num_inodes * sizeof(struct wfs_inode));
    if (st.st_size < required_size) {
        fprintf(stderr, "Error: Disk image file is too small to accommodate the specified number of blocks\n");
        exit(EXIT_FAILURE);
    }

    superblock = (struct wfs_sb*) malloc(sizeof(struct wfs_sb));
    superblock->num_inodes = num_inodes;
    superblock->num_data_blocks = num_blocks;
    superblock->i_bitmap_ptr = sizeof(struct wfs_sb);
    superblock->d_bitmap_ptr = superblock->i_bitmap_ptr + (num_inodes / 8);
    superblock->i_blocks_ptr = superblock->d_bitmap_ptr + (num_blocks / 8);
    superblock->d_blocks_ptr = superblock->i_blocks_ptr + (BLOCK_SIZE * num_inodes);


    // char *mem = mmap(NULL, (size_t) required_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	// if (mem == (void *)-1) 
	// 	perror("mmap");


    printf("inodes: %ld\n", superblock->num_inodes);

    
    lseek(fd, 0, SEEK_SET);
    if (write(fd, superblock, sizeof(struct wfs_sb)) != sizeof(struct wfs_sb)) {
        fprintf(stderr, "Error writing superblock to disk image file: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }


    // // Initialize root inode
    struct wfs_inode* root_inode = (struct wfs_inode*) malloc(sizeof(struct wfs_inode));

    // Initialize root_inode as needed
    root_inode->num     = 0;
    root_inode->size    = 0;
    root_inode->nlinks  = 0;
    root_inode->mode = S_IRWXU | S_IRWXG  | S_IRWXO;
    root_inode->gid = getgid();
    root_inode->uid = getuid();
    root_inode->atim = time(NULL);
    root_inode->mtim = time(NULL);
    root_inode->ctim = time(NULL);

    lseek(fd, superblock->i_blocks_ptr, SEEK_SET);
    // Write root inode to disk image file
    if (write(fd, root_inode, sizeof(root_inode)) != sizeof(root_inode)) {
        fprintf(stderr, "Error writing root inode to disk image file: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }


    // HELP ME HERE //
    lseek(fd, superblock->i_bitmap_ptr, SEEK_SET);
    char * bitmap = (char *)0x01;
    // Write root inode to disk image file
    if (write(fd, &bitmap, sizeof(char)) != sizeof(char)) {
        fprintf(stderr, "Error writing root inode to disk image file: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    
    close(fd);

    
    return 0;
}
