#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "filesys/off_t.h"
#include "threads/interrupt.h"
#include <stdint.h>

/* Maximum number files in a directory, 16 comes from filesys.c:do_format. */
#define MAX_ENTRIES_IN_DIR 16

void syscall_init (void);
typedef int pid_t;

/* Validate user passed-in address. Call syscall_p_exit with -1 status code if invalid.*/
void validate_addr (const void *);
/* Fetch n arguments from the frame, and fill them into the arg array. */
void syscall_retrieve_args (struct intr_frame *f, uint32_t *arg, int n);
/* Exit of the current process (by user or kernel) will call on this function.
 * Inform the status of the process to its parent/child (posthumously),
 * and delegate rest of the work to thread_exit ().*/
void syscall_p_exit (char* p_name, uint32_t status);
/* Iterates over the thread's file list, returning the file_item that matches fd */
struct file_data *get_file_data (int fd) ;

/* Syscall helper functions. */
int syscall_practice(int i);
void syscall_halt(void);
void syscall_exit(int status);
pid_t syscall_exec(const char *cmd_line); // Not implemented.
int syscall_wait(pid_t pid); // Not implemented.
bool syscall_create (const char *name, off_t initial_size); // Not implemented.
bool syscall_remove (const char *file);
int syscall_open (const char *file);
off_t syscall_filesize (int fd);
off_t syscall_read (int fd, void *buffer, off_t size);
off_t syscall_write (int fd, const void *buffer, off_t size);
void syscall_seek (int fd, off_t new_pos);
off_t syscall_tell (int fd);
void syscall_close (int fd);
bool syscall_chdir (const char *dir);
bool syscall_mkdir (const char *dir);
bool syscall_readdir (int fd, char *name);
bool syscall_isdir (int fd);
int syscall_inumber (int fd);
int syscall_pdiskread(void);
int syscall_pdiskwrite(void);
void syscall_cachereset(void);

#endif /* userprog/syscall.h */
