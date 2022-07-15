#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* Part 2 include. */
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"

/* Part 3 include. */
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "filesys/off_t.h"
#include "filesys/cache.h"

#define MAX_ARG 3 // maximum number of syscall arguments

static void syscall_handler (struct intr_frame *);
static int get_user (const uint8_t *uaddr);
bool validate_buffer (void * buffer_ptr, off_t size);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f)
{
  validate_addr((const void *) f->esp);
  uint32_t argv[MAX_ARG];
  uint32_t* args = ((uint32_t*) f->esp);
  uint32_t syscall_num = args[0];
  if (syscall_num > SYS_INUMBER)
  {
    syscall_p_exit (thread_current ()->name, -1);
  }
  switch (syscall_num) {
    case SYS_HALT:
    {
      syscall_halt ();
      // Not reached.
      break;
    }
    case SYS_EXIT:
    {
      syscall_retrieve_args (f, argv, 1);
      f->eax = argv[0];
      syscall_exit (argv[0]);
      // Not reached.
      break;
    }
    case SYS_EXEC:
    {
      syscall_retrieve_args (f, argv, 1);
      f->eax = syscall_exec ((const char *) argv[0]);
      break;
    }
    case SYS_WAIT:
    {
      syscall_retrieve_args (f, argv, 1);
      f->eax = syscall_wait (argv[0]);
      break;
    }
    case SYS_PRACTICE:
    {
      syscall_retrieve_args (f, argv, 1);
      f->eax = syscall_practice (argv[0]);
      break;
    }
    case SYS_CREATE:
    {
      syscall_retrieve_args (f, argv, 2);
      f->eax = syscall_create ((const char *) argv[0], argv[1]);
      break;
    }
    case SYS_REMOVE:
    {
      syscall_retrieve_args (f, argv, 1);
      f->eax = syscall_remove ((const char *) argv[0]);
      break;
    }
    case SYS_OPEN:
    {
      syscall_retrieve_args(f, argv, 1);
      f->eax = syscall_open ((const char *) argv[0]);
      break;
    }
    case SYS_FILESIZE:
    {
      syscall_retrieve_args (f, argv, 1);
      f->eax = syscall_filesize (argv[0]);
      break;
    }
    case SYS_READ:
    {
      syscall_retrieve_args (f, argv, 3);
      f->eax = syscall_read (argv[0], (void *) argv[1], argv[2]);
      break;
    }
    case SYS_WRITE:
    {
      syscall_retrieve_args (f, argv, 3);
      f->eax = syscall_write (argv[0], (const void *) argv[1], argv[2]);
      break;
    }
    case SYS_SEEK:
    {
      syscall_retrieve_args (f, argv, 2);
      syscall_seek (argv[0], argv[1]);
      break;
    }
    case SYS_TELL:
    {
      syscall_retrieve_args (f, argv, 1);
      f->eax = syscall_tell (argv[0]);
      break;
    }
    case SYS_CLOSE:
    {
      syscall_retrieve_args (f, argv, 1);
      syscall_close (argv[0]);
      break;
    }
    case SYS_CHDIR:
    {
      syscall_retrieve_args (f, argv, 1);
      f->eax = syscall_chdir (argv[0]);
      break;
    }
    case SYS_MKDIR:
    {
      syscall_retrieve_args (f, argv, 1);
      f->eax = syscall_mkdir (argv[0]);
      break;
    }
    case SYS_READDIR:
    {
      syscall_retrieve_args (f, argv, 2);
      f->eax = syscall_readdir (argv[0], argv[1]);
      break;
    }
    case SYS_ISDIR:
    {
      syscall_retrieve_args (f, argv, 1);
      f->eax = syscall_isdir (argv[0]);
      break;
    }
    case SYS_INUMBER:
    {
      syscall_retrieve_args (f, argv, 1);
      f->eax = syscall_inumber (argv[0]);
      break;
    }
    case SYS_PDISKREAD:
    {
      f->eax = syscall_pdiskread ();
      break;
    }
    case SYS_PDISKWRITE:
    {
      f->eax = syscall_pdiskwrite ();
      break;
    }
    case SYS_CACHERESET:
    {
      syscall_cachereset ();
      break;
    }
  }
}

void
validate_addr (const void * uaddr)
{
  int valid = (uaddr) && (is_user_vaddr (uaddr)
            && (pagedir_get_page (thread_current ()->pagedir, uaddr)));
  if ((!valid)) {
    syscall_p_exit (thread_current () -> name, -1);
  }
  // Now we check if the uaddr is a pointer and the content is not valid.
  int content = get_user (uaddr);
  if (content == -1) {
    syscall_p_exit (thread_current () -> name, -1);
  }
}

bool
validate_buffer (void * buffer_ptr, off_t size)
{
  uint32_t size_u = (uint32_t) size;
  uint32_t ptr = (uint32_t) buffer_ptr;
  uint64_t add = (uint64_t)size_u+ptr;
  if (add > 0xC0000000)
  {
    return false;
  }
  return true;
}

/* Reads a byte at user virtual address UADDR. UADDR must be below PHYS_BASE.
    Returns the byte value if successful, -1 if a segfault occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
    : "=&a" (result) : "m" (*uaddr));
  return result;
}

void
syscall_retrieve_args (struct intr_frame *f, uint32_t *arg, int n)
{
  uint32_t *ptr;
  int i;
  for (i = 0; i < n; i++)
  {
    ptr = (uint32_t *) f->esp + i + 1;
    validate_addr ((const void*) ptr);
    arg[i] = *ptr; // Deref. here
  }
}

void
syscall_p_exit (char* p_name, uint32_t status)
{
  // This is needed for exec/wait/(potentially) file syscalls.
  printf("%s: exit(%d)\n", p_name, status);
  thread_current()->proc->exit_status = status;
  /* Terminate the thread. */
  thread_exit();
}

/* Iterates over the thread's file list, returning the file_data that matches fd */
struct
file_data *get_file_data (int fd)
{
  struct thread *t = thread_current();
  struct list_elem *e;
  for (e = list_begin(&t->file_list);
       e != list_end(&t->file_list);
       e = list_next(e))
  {
    struct file_data *f_item = list_entry(e, struct file_data, elem);
    if (fd == f_item->fd)
    {
      return f_item;
    }
  }
  return NULL;
}

int
syscall_practice (int i)
{
  i++;
  return i;
}

void
syscall_halt (void)
{
  shutdown_power_off ();
}

void
syscall_exit (int status)
{
  syscall_p_exit (thread_current()->name, status);
}

pid_t
syscall_exec(const char *cmd_line)
{
  validate_addr (cmd_line);
  int result = process_execute (cmd_line);
  return result;
}

int
syscall_wait(pid_t pid)
{
  return process_wait (pid);
}

bool
syscall_create (const char *name, off_t initial_size)
{
  validate_addr ((const void*) name);
  bool b = filesys_create (name, initial_size);
  return b;
}

bool
syscall_remove (const char *name)
{
  validate_addr ((const void*) name);
  bool b = filesys_remove (name);
  return b;
};

int
syscall_open (const char *file)
{
  struct thread *t = thread_current ();
  validate_addr ((const void*) file);

  if (!strlen(file)) return -1;

  struct inode *inode = filesys_open_inode(file);
  if (!inode) {
    return -1;
  }

  struct file_data *f_item = malloc (sizeof (struct file_data));
  if (!f_item) {
    inode_close(inode);
    return -1;
  }
  if (inode_is_dir(inode)) {
    f_item->dir = dir_open (inode);
    f_item->file = NULL;
  } else {
    f_item->file = file_open (inode);
    f_item->dir = NULL;
  }

  f_item->fd = t->max_fd;
  t->max_fd += 1;

  list_push_back (&t->file_list, &f_item->elem);
  return f_item->fd;
};

off_t
syscall_filesize (int fd)
{
  struct file_data *f_item = get_file_data (fd);
  off_t o = -1;
  if (f_item) {
    struct file *f = f_item->file;
    o = file_length (f);
  }
  return o;
}

off_t
syscall_read (int fd, void *buffer, off_t size)
{
  validate_addr (buffer);
  bool valid = validate_buffer (buffer, size);
  if (!valid) return 0;
  if (fd == STDIN_FILENO)
  {
    uint8_t *charBuffer = (uint8_t *) buffer;
    int i;
    for (i = 0; i < size; i++)
    {
      charBuffer[i] = input_getc ();
    }
    return size;
  }

  struct file_data *f_item = get_file_data (fd);
  off_t o = -1;
  if (f_item)
  {
    if (f_item->dir) return 0;
    struct file *f = f_item->file;
    o = file_read (f, buffer, size);
  }

  return o;
};

off_t
syscall_write (int fd, const void *buffer, off_t size)
{
  validate_addr (buffer);
  bool valid = validate_buffer (buffer, size);
  if (!valid) return 0;

  if (fd == STDOUT_FILENO)
  {
    putbuf (buffer, size);
    return size;
  }

  struct file_data *f_item = get_file_data (fd);
  off_t o = -1;
  if (f_item) {
    if (!f_item->dir) {
      struct file *f = f_item->file;
      o = file_write (f, buffer, size);
    }
  }

  return o;
};

void
syscall_seek (int fd, off_t new_pos)
{
  struct file_data *f_item = get_file_data (fd);
  if (f_item)
  {
    struct file *f = f_item->file;
    file_seek (f, new_pos);
  }
};

off_t
syscall_tell (int fd)
{
  struct file_data *f_item = get_file_data (fd);
  off_t o = -1;
  if (f_item)
  {
    struct file *f = f_item->file;
    o = file_tell (f);
  }
  return o;
};

void
syscall_close (int fd)
{
  struct file_data *f_item = get_file_data (fd);
  if (f_item)
  {
    if (f_item->dir) {
      dir_close(f_item->dir);
    } else {
      file_close(f_item->file);
    }
    struct list_elem e = f_item->elem;
    list_remove (&e);
  }
};

bool
syscall_chdir (const char *dir_name)
{
  struct inode *inode = filesys_open_inode(dir_name); // Here the inode is not cwd->inode ?!

  if (!inode) {
    return false;
  } else {
    dir_close(thread_current()->cwd);
    thread_current ()->cwd = dir_open(inode);
    return true;
  }
}

bool syscall_mkdir (const char *name) {
  validate_addr((const void*) name);
  if (!strlen(name)) return false;
  char directory[strlen(name)];
  char filename[strlen(name)];
  split_file_name(name, directory, filename);

  block_sector_t inode_sector = 0;
  struct dir *dir;
  if (strlen(directory) == 0 && strlen(filename) == 0) {
    dir = dir_open_root();
  }
  else {
    dir = dir_open(filesys_open_inode(directory));
  }

  struct inode * inode;
  bool success = (dir != NULL
                  // && !dir_lookup(dir, filename, &inode)
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, MAX_ENTRIES_IN_DIR)
                  && dir_add (dir, filename, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  dir_close(dir);
  return success;
}

bool syscall_readdir (int fd, char *name) {
  validate_addr(name);

  struct file_data *f_item = get_file_data (fd);
  struct dir *dir = f_item->dir;
  bool success = dir_readdir(dir, name);
  return success;
}

bool syscall_isdir (int fd) {
  struct file_data *f_item = get_file_data (fd);
  return f_item->dir != NULL;
}

int syscall_inumber (int fd) {
  struct file_data *f_item = get_file_data (fd);
  int inumber;
  if (f_item->dir != NULL) {
    inumber = inode_get_inumber(dir_get_inode(f_item->dir));
  } else {
    inumber = inode_get_inumber(file_get_inode(f_item->file));
  }
  return inumber;
}

int
syscall_pdiskread (void) {
  return p_diskreads ();
}

int
syscall_pdiskwrite (void) {
  return p_diskwrites ();
}

void
syscall_cachereset (void) {
  cache_reset ();
}


/* Dumps file data for current thread. */
struct
file_data *dump_file_data (int fd)
{
  struct thread *t = thread_current();
  struct list_elem *e;
  for (e = list_begin(&t->file_list);
       e != list_end(&t->file_list);
       e = list_next(e))
  {
    struct file_data *f_item = list_entry(e, struct file_data, elem);
    // printf(f_item, )
  }
  return NULL;
}
