#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include "disk.h"

#define FAT_EOF -2
#define FAT_FREE -1
#define DIR_USED -1
#define DIR_NOT_USED 0
#define FD_USED DIR_USED
#define FD_NOT_USED DIR_NOT_USED
#define DISK_ACTIVE -1
#define DISK_INACTIVE 0

#define MAX_FILEDES 32
#define MAX_F_NAME 15
#define MAX_NUM_FILES 64
#define MAX_FILE_SIZE (4096 * BLOCK_SIZE)

#define DATA_START_BLOCK 4096
#define DATA_SIZE_BLOCKS 4096
#define FAT_START_BLOCK 1
#define FAT_SIZE_BLOCKS (((sizeof(int)*DATA_SIZE_BLOCKS) - 1) /BLOCK_SIZE) + 1
#define DIR_START_BLOCK (FAT_START_BLOCK + FAT_SIZE_BLOCKS)
#define DIR_SIZE_BLOCKS (((sizeof(struct dir_entry) * MAX_NUM_FILES) - 1) /BLOCK_SIZE) + 1

struct super_block {
    int fat_idx; // First block of the FAT
    int fat_len; // Length of FAT in blocks
    int dir_idx; // First block of directory
    int dir_len; // Length of directory in blocks
    int data_idx; // First block of file-data
    int disk_active;
    char name [MAX_F_NAME + 1]; 
};

struct dir_entry {
    int used; // Is this file-”slot” in use
    char name [MAX_F_NAME + 1]; // DOH!
    int size; // file size
    int head; // first data block of file
    int ref_cnt;
    // how many open file descriptors are there?
    // ref_cnt > 0 -> cannot delete file
};

struct file_descriptor {
    int used; // fd in use
    int file; // the first block of the file
    // (f) to which fd refers too
    int offset; // position of fd within f
};

struct super_block fs;
struct file_descriptor FDLIST[MAX_FILEDES]; // 32
int *FAT; // Will be populated with the FAT data
struct dir_entry *DIR; // Will be populated with the directory data


int make_fs(char *disk_name);
int mount_fs(char *disk_name);
int umount_fs(char *disk_name);
int fs_open(char *name);
int fs_close(int fildes);
int fs_create(char *name);
int fs_delete(char *name);
int fs_read(int fildes, void *buf, size_t nbyte);
int fs_write(int fildes, void *buf, size_t nbyte);
int fs_get_filesize(int fildes);
int fs_listfiles(char ***files);
int fs_lseek(int fildes, off_t offset);
int fs_truncate(int fildes, off_t length);
int find_empty_block();
int find_empty_dir_entry();
int find_empty_file_desc();


int make_fs(char *disk_name){   
    
    if(make_disk(disk_name) < 0){
        return -1;
    }

    
    if(open_disk(disk_name) < 0){
        return -1;
    }

    fs.fat_idx = FAT_START_BLOCK;
    fs.fat_len = FAT_SIZE_BLOCKS;
    fs.dir_idx = DIR_START_BLOCK;
    fs.dir_len = DIR_SIZE_BLOCKS;
    fs.data_idx = DATA_START_BLOCK;
    fs.disk_active = DISK_INACTIVE;
    strcpy(fs.name, disk_name);
    
    char writebuf[BLOCK_SIZE];
    memcpy(writebuf, &fs, sizeof(fs));
    if(block_write(0, writebuf) < 0){
        return -1;
    }

    FAT = calloc(fs.fat_len, BLOCK_SIZE);
    DIR = calloc(fs.dir_len, BLOCK_SIZE);


    int i;
    for(i = 0; i < DATA_SIZE_BLOCKS; i++){
        FAT[i] = FAT_FREE;
    }
    

    if(block_write(fs.fat_idx, (char*)FAT) < 0){
        return -1;
    }
    free(FAT);

    struct dir_entry temp = {.used = DIR_NOT_USED, .size = 0, .head = -1, .ref_cnt = 0};
    for(i = 0; i < MAX_NUM_FILES; i++){
        DIR[i] = temp;
    }

    if(block_write(fs.dir_idx, (char*)DIR) < 0){
        return -1;
    }
    free(DIR);


    if(close_disk(disk_name) < 0){
        return -1;
    }

    return 0;
}

int mount_fs(char *disk_name){
    if(open_disk(disk_name) < 0){
        return -1;
    }

    char readbuf[BLOCK_SIZE];
    if(block_read(0, readbuf) < 0){
        return -1;
    }
    memcpy(&fs, readbuf, sizeof(fs));
    fs.disk_active = DISK_ACTIVE;
    memcpy(readbuf, &fs, sizeof(fs));
    if(block_write(0, readbuf) < 0){
        return -1;
    }

    FAT = calloc(fs.fat_len, BLOCK_SIZE);
    DIR = calloc(fs.dir_len, BLOCK_SIZE);

    if(block_read(fs.fat_idx, (char*)FAT) < 0){
        return -1;
    }
    
    if(block_read(fs.dir_idx, (char*)DIR) < 0){
        return -1;
    }


    return 0;
}

int umount_fs(char *disk_name){

    if(disk_name == NULL){
        return -1;
    }
    if(strcmp(disk_name, fs.name) != 0){
        return -1;
    }
    if(fs.disk_active == DISK_INACTIVE){
        return -1;
    }

    int i;
    for(i = 0; i < MAX_FILEDES; i++){
        fs_close(i);    
    }

    char writebuf[fs.fat_len * BLOCK_SIZE];
    memcpy(writebuf, FAT, sizeof(writebuf));
    if(block_write(fs.fat_idx, writebuf) < 0){
        return -1;
    }
    free(FAT);

    char writebuf1[fs.dir_len * BLOCK_SIZE];
    memcpy(writebuf1, DIR, sizeof(writebuf1));
    if(block_write(fs.dir_idx, writebuf1) < 0){
        return -1;
    }
    free(DIR);
    
    char writebuf2[BLOCK_SIZE];
    memcpy(writebuf2, &fs, sizeof(fs));
    if(block_write(0, writebuf2) < 0){
        return -1;
    }

    if(close_disk() < 0){
        return -1;
    }

    return 0;
}

int fs_open(char *name){

    int i;
    for (i = 0; i < MAX_NUM_FILES; i++){
        if(strcmp(DIR[i].name, name) == 0 && (DIR[i].used == DIR_USED)){
            int fd;
            if((fd = find_empty_file_desc()) != -1){
                FDLIST[fd].used = FD_USED;
                FDLIST[fd].file = DIR[i].head;
                FDLIST[fd].offset = 0;

                DIR[i].ref_cnt++;

                return fd;
            }else{
                return -1;
            }
        }
    }
    return -1;
    
}

int fs_close(int fildes){

    if(fildes < 0 || fildes >= MAX_FILEDES){
        return -1;
    }
    if(FDLIST[fildes].used == FD_NOT_USED){
        return -1;
    }
    
    int i;
    for (i = 0; i < MAX_NUM_FILES; i++){
        if(FDLIST[fildes].file == DIR[i].head){
            DIR[i].ref_cnt--;
            FDLIST[fildes].used = FD_NOT_USED;
            FDLIST[fildes].file = 0;
            FDLIST[fildes].offset = 0;
            return 0;
        }
    }

    return -1;
}

int fs_create(char *name){

    if(sizeof(name) > sizeof(char) * MAX_F_NAME){
        return -1;
    }
    int dir_entry_slot;
    if((dir_entry_slot = find_empty_dir_entry()) == -1){
        return -1;
    }

    int fat_head_slot;
    if((fat_head_slot = find_empty_block()) == -1){
        return -1;
    }

    int i;
    for(i = 0; i < MAX_NUM_FILES; i++){
        if(strcmp(DIR[i].name,name) == 0){
            return -1;
        }
    }

    strcpy(DIR[dir_entry_slot].name, name);
    DIR[dir_entry_slot].used = DIR_USED;
    DIR[dir_entry_slot].head = fat_head_slot;
    FAT[fat_head_slot] = FAT_EOF;

    return 0;
}

int fs_delete(char *name){

    int i;
    for(i = 0; i < MAX_NUM_FILES; i++){
        if(strcmp(DIR[i].name,name) == 0){
            if(DIR[i].ref_cnt > 0){
                return -1;
            }else{
                do {
                    char writebuf[BLOCK_SIZE];
                    block_write(DIR[i].head+fs.data_idx, writebuf);
                    int temp = DIR[i].head;
                    DIR[i].head = FAT[DIR[i].head];
                    FAT[temp] = FAT_FREE;
                }while (DIR[i].head != FAT_EOF);
                

                DIR[i].used = DIR_NOT_USED;
                memset(DIR[i].name, 0, sizeof(DIR[i].name));
                DIR[i].head = -1;
                DIR[i].size = 0;

                return 0;
            }
        }
    }

    return -1;

}

//we're gonna put the whole file in a buffer then cut off the front/back
int fs_read(int fildes, void *buf, size_t nbyte){
    if(fildes < 0 || fildes >= MAX_FILEDES){
        return -1;
    }
    if(FDLIST[fildes].used == FD_NOT_USED){
        return -1;
    }
    if(nbyte == 0){
        return 0;
    }
    if(nbyte < 0){
        return -1;
    }

    int blocksToRead = ((nbyte - 1) /BLOCK_SIZE) + 1;
    int currBlock = FDLIST[fildes].file;
    char readbuf[BLOCK_SIZE*blocksToRead];
    //memset(readbuf, 0, BLOCK_SIZE*blocksToRead);
    int bytesRead = 0;
    int bytesToRead = nbyte;
    int i;
    for(i = 0; i< blocksToRead; i++){
        if(block_read(currBlock + fs.data_idx, readbuf + (BLOCK_SIZE*i)) < 0){
            return -1;
        }
        bytesRead += BLOCK_SIZE;
        bytesToRead -= BLOCK_SIZE;
        
        if(FAT[currBlock] == FAT_EOF){
            if(fs_get_filesize(fildes) > 0){
            //    bytesRead += fs_get_filesize(fildes) - bytesRead;
            }
            break;
        }else{

            currBlock = FAT[currBlock];
        }
    }

    //see if we went over nbytes (we were writing in block chunks)    
    if(bytesToRead < 0){
        bytesRead += bytesToRead;
        //if we were, correct byteswritten to be how much we actually changed in the data
    }else if(bytesToRead > 0){
        bytesRead -= FDLIST[fildes].offset;  
    }

    if(bytesRead > 0){
        memcpy(buf, (readbuf + FDLIST[fildes].offset), bytesRead);
    }
    FDLIST[fildes].offset += bytesRead;
    return bytesRead;
}

int fs_write(int fildes, void *buf, size_t nbyte){
    if(fildes < 0 || fildes >= MAX_FILEDES){
        return -1;
    }
    if(FDLIST[fildes].used == FD_NOT_USED){
        return -1;
    }

    if(nbyte == 0){
        return 0;
    }
    if(nbyte < 0){
        return -1;
    }

    int blocksToWrite = ((nbyte - 1) /BLOCK_SIZE) + 1;
    int currBlock = FDLIST[fildes].file;
    char writebuf[BLOCK_SIZE*blocksToWrite];
    //memset(writebuf, 0, BLOCK_SIZE*blocksToWrite);
    int tempOffset = FDLIST[fildes].offset;
    if(fs_lseek(fildes, 0) <0){
        return -1;
    }
    fs_read(fildes, writebuf, BLOCK_SIZE*blocksToWrite);
    if(fs_lseek(fildes, tempOffset) <0){
        return -1;
    }

    memcpy((writebuf + FDLIST[fildes].offset), buf, nbyte);
    
    int i;
    int bytesWritten = 0;
    int bytesToWrite = nbyte;
    for(i = 0; i< blocksToWrite; i++){
        if(block_write(currBlock + fs.data_idx, writebuf + (BLOCK_SIZE*i)) < 0){
            return -1;
        }
        bytesWritten += BLOCK_SIZE;
        bytesToWrite -= BLOCK_SIZE;

        if(FAT[currBlock] == FAT_EOF){
            if(bytesToWrite > 0){
                int newBlock;
                if((newBlock = find_empty_block()) == -1){
                    break;
                }else{
                    FAT[currBlock] = newBlock;
                    FAT[newBlock] = FAT_EOF;
                }
            }
        }
        currBlock = FAT[currBlock];     
    }

    //see if we went over nbytes (we were writing in block chunks)    
    if(bytesToWrite < 0){
        bytesWritten += bytesToWrite;
        //if we were, correct byteswritten to be how much we actually changed in the data
    }else if(bytesToWrite > 0){
        bytesWritten -= FDLIST[fildes].offset;  
    }
    
    //bytesWritten -= FDLIST[fildes].offset;
    FDLIST[fildes].offset += bytesWritten;

    //now have to fix size. if offset is greater than size, it will be our new size
    int dirNum;
    for(i = 0; i < MAX_NUM_FILES; i++){
        if(DIR[i].head == FDLIST[fildes].file){
            dirNum = i;
            break;
        }
    }
    if(FDLIST[fildes].offset > DIR[dirNum].size){
        DIR[dirNum].size = FDLIST[fildes].offset;
    }

   
    return bytesWritten;
}

int fs_get_filesize(int fildes){
    if(fildes < 0 || fildes >= MAX_FILEDES){
        return -1;
    }
    if(FDLIST[fildes].used == FD_NOT_USED){
        return -1;
    }

    int i;
    for(i = 0; i < MAX_NUM_FILES; i++){
        if(DIR[i].head == FDLIST[fildes].file){
            return DIR[i].size;
        }
    }
    return -1;
} 

int fs_listfiles(char ***files){

    int i;
    int curr = 0;
    for(i = 0; i < MAX_NUM_FILES; i++){
        if(DIR[i].used == DIR_USED){
            strcpy((char*)(files)+16*curr++, DIR[i].name);
        }
    }
    files[curr] = NULL;

    return 0;
}

int fs_lseek(int fildes, off_t offset){
    if(fildes < 0 || fildes >= MAX_FILEDES){
        return -1;
    }
    if(FDLIST[fildes].used == FD_NOT_USED){
        return -1;
    }

    if(offset < 0 || offset > fs_get_filesize(fildes)){
        return -1;
    }

    FDLIST[fildes].offset = offset;
    return 0;
}

int fs_truncate(int fildes, off_t length){
    if(fildes < 0 || fildes >= MAX_FILEDES){
        return -1;
    }
    if(FDLIST[fildes].used == FD_NOT_USED){
        return -1;
    }

    if(length < 0 || length > fs_get_filesize(fildes)){
        return -1;
    }
    
    

    
    if(FDLIST[fildes].offset > length){
        FDLIST[fildes].offset = length;
    }
        
    return 0;
}

//helper functions to find empty slots to put stuff in
int find_empty_block(){
    int i;
    for(i = 0; i < DATA_SIZE_BLOCKS; i++){
        if(FAT[i] == FAT_FREE){
            return i;
        }
    }

    fprintf(stderr, "find_empty_block: no free blocks\n");
    return -1;
}

int find_empty_dir_entry(){
    int i;
    for(i = 0; i < MAX_NUM_FILES; i++){
        if(DIR[i].used == DIR_NOT_USED){
            return i;
        }
    }

    fprintf(stderr, "find_empty_dir_entry: max # of files reached\n");
    return -1;
}

int find_empty_file_desc(){
    int i;
    for(i = 0; i < MAX_FILEDES; i++){
        if(FDLIST[i].used == FD_NOT_USED){
            return i;
        }
    }

    fprintf(stderr, "find_empty_file_desc: max # of open files reached\n");
    return -1;
}