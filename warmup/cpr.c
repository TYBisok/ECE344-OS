#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include "common.h"
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>

void copy_dir(char* src_dir, char* des_dir);
void copy_file(char* src, char* des);
char* path_format(char* dom, char* sub); // concatenates dom and sub to get the path

char* path_format(char* dom, char* sub) { 
    char* path = (char*)malloc( (strlen(dom) + strlen(sub) + 2) * sizeof(char));
    strcpy(path, dom);
    strcat(path, "/");
    strcat(path, sub);
    strcat(path, "\0");
    return path;
}

/* make sure to use syserror() when a system call fails. see common.h */
void usage() {
    fprintf(stderr, "Usage: cpr srcdir dstdir\n");
    exit(1);
}

// To copy a file, you will need the following system calls: 
// open, creat, read, write, and close
int main(int argc, char *argv[]) {
    // expected arguments are program, source, destination
    if (argc != 3) { 
            usage();
    }

    // any checks?
    // when running in terminal, getting "stat: no such file or directory"
 
    char* src = argv[1];
    char* des = argv[2];
   
    //printf("\nsrc input: %s \ndes input: %s \n", src, des);
 
    struct stat statbuf;  
    struct stat* statbuf_ptr;
    statbuf_ptr = &statbuf;
    
    int stat_status = stat(src, statbuf_ptr);
    
    if(stat_status == -1)
      syserror(stat, src); // however this works
 
    int mkdir_status = mkdir(des, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); //the modes were chosen from die.net

    if(mkdir_status < 0)
      syserror(mkdir, des);

    copy_dir(src, des);
    
    return 0;
}

// creat - creates the destination file
void copy_file(char* src, char* des) {
    int fd_opensrc, fd_opendes, fd_createdes; // fd = file descripto
    int ret_readsrc, ret_writedes;
  
    // create destination file
    // int creat(const char *path, mode_t mode);
    
    // before creating a file, should check if the directory is read only or not
    fd_createdes = creat(des, S_IRWXU); // ?????????

    if(fd_createdes < 0)
        syserror(creat, des); // creat failed

    //printf("success: create des\n");
  
    // open the source file
    fd_opensrc = open(src, O_RDONLY); // RDONLY - read only flag
    // open destination file
    fd_opendes = open(des, O_WRONLY); // WRONLY - write only flag
  
    if (fd_opensrc < 0) { 
        syserror(open, src);
        //exit(1); // should this be everywhere theres an error? 
    }
 
   if (fd_opendes < 0) { 
        syserror(open, des);
        //exit(1); // should this be everywhere theres an error? 
    }
   
   
   // copy from src to des
   // 
    // int ret;
    // ret = read(fd, buf, 4096); // if ret = 0, end of file has been reached
  
  char buf[4096];
  // ssize_t read(int fd, void *buf, size_t count);
  ret_readsrc = read(fd_opensrc, buf, 4096); // on success, # of bytes read is returned
  
    if(ret_readsrc < 0) {
        syserror(read, src);
    }
  
    while(ret_readsrc != 0) {
        // ssize_t write(int fd, const void *buf, size_t count);
        // count is number of bytes to write


        ret_writedes = write(fd_opendes, buf, ret_readsrc);

        if(ret_writedes < 0)
          syserror(write, des);

        ret_readsrc = read(fd_opensrc, buf, 4096);
        
        if(ret_readsrc < 0) {
            syserror(read, src);
        }
    }
       
    // close all the fds aka every open should close
    // close(fd_createdes);
    close(fd_opensrc);
    close(fd_opendes);
}

void copy_dir(char* src, char* des) {
  printf("start copy_dir\n");
  
  DIR* src_dir = opendir(src); // open src directory 
  
  if (src_dir == NULL)
    syserror(opendir, src); // syserror(syscall, file)
    
  //printf("success: open source dir\n");
  
  struct dirent* dir_info;
  dir_info = readdir(src_dir); // get the info on current directory 
  
  
  //printf("success: read source dir\n");
  //printf("69\n");
  
    while(dir_info != NULL) {
        // file/dir pathname is given by src_dir/(file or dir)

        char* filename = dir_info->d_name;

        //printf("filename: %s\n", filename);

        if(strcmp(dir_info->d_name, ".") == 0 || strcmp(dir_info->d_name, "..") == 0) {
            ;
        }
        else {
            char* src_path = path_format(src, filename);
            char* des_path = path_format(des, filename);


            // now figure out what the types of these paths are are 
            // int stat(const char *pathname, struct stat *statbuf);
            // On success, zero is returned.  On error, -1 is returned, and errno is set appropriately.
            // this struct contains
            //    mode_t    st_mode;        // File type and mode

            struct stat statbuf;  
            struct stat* statbuf_ptr;
            statbuf_ptr = &statbuf;

            int stat_status = stat(src_path, statbuf_ptr);

            int file_mode = statbuf.st_mode;

            if(stat_status < 0)
                syserror(stat, src_path); // however this works

            // printf("success: stat source dir\n");

            // now split off between file and directory
            // use calls below where m is the st_mode field
            // S_ISREG(m) - is it a regular file?
            // S_ISDIR(m) - directory?

            // then - regarless of the type, set permissions of the file
            // int chmod(const char *path, mode_t mode);

            if(S_ISREG(file_mode)) { // then copy file
                // printf("copy FILE %s from SRC %s to DES %s\n", filename, src_path, des_path);
                copy_file(src_path, des_path);
                
                // CHECK IF DIRECTORY HAS READ PERMISSIONS
                chmod(des_path, file_mode); // do this after copying cause then you can write to it even if read only

            }
            else if (S_ISDIR(file_mode)) { // recursively copy this directory
                // printf("copy DIR %s from SRC %s to DES %s\n", filename, src_path, des_path);

                // first make the directory 
                // int mkdir(const char *pathname, mode_t mode);
                    // mode specifies the mode for the new directory
                    // return zero on success, or -1 if an error occurred

                int mkdir_status = mkdir(des_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH); //the modes were chosen from die.net

                if(mkdir_status < 0)
                  syserror(mkdir, des_path);

                //printf("success: make dir at des - %s\n", des_path);

                
                copy_dir(src_path, des_path);
                
                chmod(des_path, file_mode); // do this after copying cause then you can write to it even if read only
            } 
        } // end else of checking ..
        dir_info = readdir(src_dir); // read next element
    }
    
    
    closedir(src_dir); // close source directory
}


/* 
-----------notes/pseudocode-----------
main
  // opendir - open directory
  // readdir - read directory entries one by one
  
  // directory entry structure refers to either a file or a sub-directory
  // it doesn't tell us if it's a file or dir
  // "stat" system call - takes pathname and returns a stat structure that provides types of info 
  // "st_mode" field - the file TYPE and PERMISSIONS are encoded in this field
  // use the S_ISREG and S_ISDIR macros on this field to determine the type of the file/directory (ignore all other types) (see the man page of stat)
  // also need to extract permissions from "st_mode"
  // then use chmod to set the same permissions on the target file or directory 
  // so need to create a struct
  
  
  // PSEUDOCODE
  // CALL THIS FUNC COPY_DIR
  // parameters list:
  // - char* src_dir;
  // - char* des_dir;
  
  // ---OPEN DIRECTORY---
  // DIR *opendir(const char *name); // returns pointer to the dir stream, returns NULL on error
  
  DIR* src_dir = opendir(src);
  if (src_dir == NULL)
    syserror; // dk how this works
  
  // now read each entry in directory 
  // struct dirent *readdir(DIR *dirp);
  
  // struct dirent {
  //  ino_t          d_ino;       // inode number 
  //  off_t          d_off;       // offset to the next dirent 
  //  unsigned short d_reclen;    // length of this record 
  //  unsigned char  d_type;      // type of file; not supported
                                   by all file system types 
  //  char           d_name[256]; /// filename 
  //};
  
  
  
  struct dirent* dir_info;
  
  while((dir_info = readdir(src_dir) != NULL) {
    // file/dir pathname is given by src_dir/(file or dir)
    
    char src_path[1000], des_path[1000];
    
    src_path = path(source, dir_info->d_name);
    des_path = path(destination, dir_info->d_name);
  
    // now figure out what the types of these paths are are 
    // int stat(const char *pathname, struct stat *statbuf);
    // On success, zero is returned.  On error, -1 is returned, and errno is set appropriately.
    // this struct contains
    //    mode_t    st_mode;        // File type and mode
    
    struct stat statbuf;  
    struct stat* statbuf_ptr;
    statbuf_ptr = &statbuf;
    
    int stat_status = stat(src_path, statbuf_ptr);
    
    int file_mode = statbuf.st_mode;
    
    if(stat_status == -1)
      syserror(); // however this works
    else { // now split off between file and directory
      // use calls below where m is the st_mode field
      // S_ISREG(m) - is it a regular file?
      // S_ISDIR(m) - directory?
      
      // then - regarless of the type, set permissions of the file
      // int chmod(const char *path, mode_t mode);
      
      if(S_ISREG(file_mode)) {
        // then copy file
        copy_file(src_path, des_path);
        chmod(des_path, file_mode);
      }
      else if (S_ISDIR(file_mode)) {
        // recursively copy this directory
        // first make the directory 
        // int mkdir(const char *pathname, mode_t mode);
        // mode specifies the mode for the new directory
        // return zero on success, or -1 if an error
       occurred
       
       int mkdir_status = mkrdir(des_path, ????????);
       
       if(mkdir_status == -1)
         syserror();
       else {
          copy_dir(src_path, des_path);
          chmod(des_path, file_mode);
       }
         
      } 
      
      
      
    
    }
    closedir(dir_info);
  }
  

*/
