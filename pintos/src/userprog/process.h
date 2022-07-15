#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
typedef int pid_t;

/* Associates pid with process data */
struct proc_data {
  pid_t pid;
  int load_status;
  int exit_status;
  struct file *exe;
  struct semaphore sema;
  struct list_elem elem;
};

/* Associates fd with file data */
struct file_data {
  int fd;
  // Either `file` or `dir` will be non-NULL, depending on
  // whether fd represents a file or dir
  struct file *file;
  struct dir *dir;
  struct list_elem elem;
};

struct token {
  struct list_elem list_element;
  char token[256];
};

struct start_process_args {
  char file_name[256];
};


tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
