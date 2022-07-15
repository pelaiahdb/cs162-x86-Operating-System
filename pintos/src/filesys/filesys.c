#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Maximum length of a file path */
#define PATH_MAX (NAME_MAX+1)*20

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Parser function that splits the path into components by '/'. Return 1 if successful,
   0 at the end of the string, -1 for file name component that is too long. */
static int
get_next_part (char part[NAME_MAX + 1], const char **srcp) {
  const char *src = *srcp;
  char *dst = part;
  /* Skip leading slashes. If the entire path consists of slashs, return 0; */
  while (*src == '/') {
    src++;
  } if (*src == '\0') {
    return 0;
  }
  /* Copy a component of up to NAME_MAX character from SRC to DST, and add NULL terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX) {
      *dst++ = *src;
    } else {
      // name too long
      return -1;
    }
    src++;
  }
  *dst = '\0';
  /* Advance the source ptr. */
  *srcp = src;
  return 1;
}

/* Split a path into parent directories and destination file/folder. */
void
split_file_name(const char *path, char* directory, char *filename) {
  int l = strlen(path);
  char *s = (char*) malloc( sizeof(char) * (l + 1) );
  memcpy (s, path, sizeof(char) * (l + 1));

  // absolute path handling
  char *dir = directory;
  if(l > 0 && path[0] == '/') {
    if(dir) *dir++ = '/';
  }

  // tokenize
  char *token, *p, *last_token = "";
  for (token = strtok_r(s, "/", &p); token != NULL;
       token = strtok_r(NULL, "/", &p))
  {
    // append last_token into directory
    int tl = strlen (last_token);
    if (dir && tl > 0) {
      memcpy (dir, last_token, sizeof(char) * tl);
      dir[tl] = '/';
      dir += tl + 1;
    }

    last_token = token;
  }

  if(dir) *dir = '\0';
  memcpy (filename, last_token, sizeof(char) * (strlen(last_token) + 1));
  free (s);
}

/* Recursively looks up a file path
 * (including subdirectories) FILE_PATH, which can be either absolute (start with ‘/’) or relative.
 * Returns true if one exists.
 * On success, sets *INODE to an inode for the file,
 * otherwise to a null pointer.
 * The caller must close *INODE. */
bool
recursive_path_lookup (const char *file_path, struct inode **inode_) {
  struct inode* inode;
  struct dir *dir;
  char *part = malloc(sizeof(char) * (NAME_MAX + 1));
  const char **srcp = &file_path;
  if (file_path[0] == '/') { // is absolute path
    dir = dir_open_root();
  } else {
    dir = thread_current()->cwd != NULL ? dir_reopen(thread_current()->cwd) : dir_open_root();
  }
  inode = dir_get_inode(dir); // needed for cd '/'

  bool exists;
  while (get_next_part(part, srcp)) {
    exists = dir_lookup(dir, part, &inode);
    if (!exists) {
      dir_close(dir);
      inode_close(inode);
      *inode_ = NULL;
      free(part);
      return false;
    } else if (inode_is_dir(inode)) {
      dir_close(dir);
      dir = dir_open(inode);
    }
    else {
      dir_close(dir);
      break;
    }
  }
  *inode_ = inode;
  free(part);
  return true;
}

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  cache_init ();

  if (format)
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();

  cache_done ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  block_sector_t inode_sector = 0;
  if (!strlen(name)) return false;
  char directory[strlen(name)];
  char filename[strlen(name)];
  split_file_name(name, directory, filename);

  struct dir *dir;
  if (strlen(directory) != 0) {
    dir = dir_open(filesys_open_inode(directory));
  }
  else {
    dir = thread_current()->cwd != NULL ? dir_reopen(thread_current()->cwd) : dir_open_root();
  }
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, filename, inode_sector));

  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);

  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct inode* inode = filesys_open_inode(name);
  return file_open (inode);
}

/* Opens inode corresponding to NAME. */
struct inode *
filesys_open_inode (const char *name) {
  struct inode* inode;
  // if (strcmp(name, "") == 0) return NULL;
  recursive_path_lookup(name, &inode);
  return inode;
}

/* Deletes the file or directory named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  if (strcmp(name, "/") == 0) return false;
  char directory[strlen(name)];
  char filename[strlen(name)];
  split_file_name(name, directory, filename);

  struct dir *dir;
  if (strlen(directory) == 0) {
    dir = thread_current()->cwd != NULL ? dir_reopen(thread_current()->cwd) : dir_open_root();
  } else {
    dir = dir_open(filesys_open_inode(directory));
  }

  bool success = dir != NULL && dir_remove (dir, filename);
  dir_close (dir);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
