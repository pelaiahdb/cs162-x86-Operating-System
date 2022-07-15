#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

/* Maximum number of entries per directory. 16 taken from <filesys/filesys.c> */
#define MAX_ENTRIES_IN_DIR 16

struct inode;

/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup (const struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, block_sector_t);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);

/* Debug function that can be called from inside gdb. Show contents of a directory. */
void dir_dump (struct dir *dir);

/* Split a path into parent directories and destination file/folder. Used by filesys_create. 
	 char * directory and char * filename should be pre-allocated buffers. */
void split_file_name(const char *path, char* directory, char *filename);

/* Opens the directory for the given path. */
struct dir * dir_open_path (const char* file_path);

#endif /* filesys/directory.h */
