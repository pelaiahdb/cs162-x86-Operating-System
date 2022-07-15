Design Document for Project 3: File System
==========================================

## Group Members

* Feynman Liang <feynman@berkeley.edu>
* Pelaiah Blue <pelaiahdavyblue@berkeley.edu>
* Kevin Lu <klu95@berkeley.edu>
* Muliang Shou <muliang.shou@berkeley.edu>

# Task 1: Buffer cache

## Data Structures and Functions

```c
/* Cache block/line of buffer cache */
struct cache_block
{
    struct list_elem list_elem; // Store as a List element
    block_sector_t disk_block; // This is just a uint32_t index of the block in disk
    char memory_block[512]; // 512-byte block in memory
    bool recently_used; // initially false; set to false on miss, set to true on hit
    bool dirty_bit; // true if block was modified
    bool being_used; // true when cache block is being: read, written, evicted, flushed

  uint_8 shared_or_ex; // If greater than 1, there's a shared lock on this block. If 0, there's an exclusive lock on this block. If 1, it’s free.

};
```

```c
struct list buffer_cache; // Global buffer cache (max size = 64 sectors)
```

We will edit the following `inode` functions:

```c
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
...
/* We replace calls to `block_read` with a wrapper that performs this read on the buffer cache */
...
}
```

```c
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
...
/* We replace calls to `block_write` with a wrapper that performs writes on the buffer cache */
...
}
```

We introduce wrapper functions to `block_read` and `block_write` that use the buffer cache.

```c
void
cache_block_read (struct block *block, block_sector_t sector, void *buffer)
{
  check_sector (block, sector);
  block->ops->read (block->aux, sector, buffer);
  block->read_cnt++;
}
```

```c
void
cache_block_write (struct block *block, block_sector_t sector, const void *buffer)
{
  check_sector (block, sector);
  ASSERT (block->type != BLOCK_FOREIGN);
  block->ops->write (block->aux, sector, buffer);
  block->write_cnt++;
}
```

For flushing dirty block upon shutdown, we modify:

```c
void filesys_done (void)
{
...
}
```

## Algorithms

We use the Clock Algorithm in lecture.

If a page fault occurs:

1. Check if free slot exists.
  * If yes, then read block from disk and save to buffer cache.
  * If no free slot exists, advance clock hand (a pointer to the last element in the list).
2. Check if current slot was recently used (if recently_used is true).
  * If recently_used, change it to false, and advance clock hand.
  * If slot is not recently used, evict slot and replace with new block.

For Write-Back, upon eviction we flush the block to disk before replacing.

For calls to `inode_write_at`:

1. Check if in buffer cache:
 * If yes, write to cache block entry.
 * If no, get a free slot using Clock Algorithm and call `block_write`.

For calls to `inode_read_at`:

1. Check if in buffer cache:
 * If yes, read cache block
 * If no, get a free slot using Clock Algorithm and call `block_read`.

## Synchronization

To prevent eviction of an actively written/read block entry, we introduce a
`bool being_used` in `struct cache_block` that acts as a lock.

During eviction, `being_used` is set to prevent other processes from accessing
it.

Before loading a block into the cache, we check if the block is already
written/being written into the cache by another process. `being_used` is set
until the block is fully loaded.

Concurrent read requests should be allowed, and each write_request should have
an exclusive lock on the block. To implement this, we introduce a `uint8_int
shared_or_ex` that acts as a lock that is either shared or exclusive. Read
requests try to acquire a shared lock (lock > 1), while write requests acquire
an exclusive lock (lock == 0). If the object has a shared lock, all read
requests are allowed access while write requests must wait. If the object has
an exclusive lock, only one write request is processed on that entry and no
reads.

## Rationale

We explored the idea of using the PintOS hash table for the buffer cache;
however it proved to be more difficult to implement when choosing which entry
to evict. We use a list of `struct cache_block` as the buffer cache to closely
follow the Clock Algorithm presented in lecture.

We have not experimented yet on the metadata combination that would result in
the least disk accesses, so we decided to start with the `recently_used` and
`dirty_bit` elements needed by the Clock Algorithm.

Instead of creating a lock object that can either be shared or exclusive, we
opted to use a `uint8_t` to minimize space.

# Task 2: Extensible files

## Data Structures and Functions

```c
struct inode_disk {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;
-   uint32_t unused[125];               /* Not used. */
+  block_sector_t direct[122];
+  block_sector_t indirect;
+  block_sector_t doubly_indirect;
}
```

```c
struct inode {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
-   struct inode_disk data;             /* Inode content. */
+  struct lock inode_lock;  /* To handle concurrent access to the inode struct */
}
```

Add support for `inumber` syscall

```c
...
case SYS_INUMBER   :
{
    syscall_retrieve_args (f, argv, 1);
    inumber (argv[0]);
    break;
}

...
int inumber (int fd);
```

## Algorithms

We eliminate the external fragmentation problem by using a multilevel index
structure. In the `inode_disk` struct, we add pointers to `direct`, `indirect`,
and `doubly_indirect` blocks. Consequently we no longer need the `data` pointer
in `inode`. Instead, the inode `sector` field gives us access to the
corresponding `inode_disk`, which itself has access to the data blocks.

To implement file growth, `inode_write_at` needs to be modified to extend the
file when necessary and write the extended file back to the cache.

We can use `byte_to_sector` to determine if an inode contains data at a given
position. This is useful for knowing when we are writing past the EOF. However,
we need to modify this function to use the `inode_disk` struct instead of the
`inode` data pointer.

If we are writing past the EOF, we can then use `free_map_allocate` to extend
the file to the position being written. However, we must first check if there
enough free blocks available. If we are unable to allocate new disk blocks,
such as in the case of disk space exhaustion, we will abort file extension
gracefully.

## Synchronization

Multiple threads may access the same inodes. Multiple processes may attempt to
extend a file at the same time. To handle concurrent access, we add a lock to
the `inode struct`, which must be acquired before writing to any of the fields
or extending a file.

## Rationale

We chose to use an index structure with direct, indirect, and doubly indirect
blocks. We are already familiar with this scheme from lecture, and believe this
to be the simplest implementation.

Reading from indirect pointers requires many memory accesses and is slow;
direct pointers are much faster. We need lots of direct pointers so
small/medium files can be read quickly, but keep a few indirect pointers to
support large files if needed.

In our `inode_disk` struct we only store many direct pointers, but only one
indirect pointer and one doubly indirect pointer. Assuming a block size of 512
B (2^9 B), an indirect block can handle 65536 B (2^7 * 2^9), and a
doubly_indirect block can handle 8 MB by itself (2^7 * 2^7 * 2^9). Thus, for
our implementation to support a maximum filesize of 8 MB, a single
doubly-indirect block and a single indirect block is sufficient. We fill the
remaining “unused space” with more than enough direct pointers, because we
might as well use that extra space for something.


# Task 3: Subdirectories

## Data Structures and Functions

Since each process must maintain a separate current working directory, we
extend `struct thread` to keep track of this:

```c
// threads/thread.h
struct thread {
  ...
  struct dir * cwd
  ...
}
```

To support “..”, we make sure that directories are able to track their parent

```c
// filesys/directory.c
struct dir {
  ...
  struct dir* parent; // parent directy, NULL if `dir` is ‘/’
  ...
}
```

At startup, set the file system root as the initial process’s current directory
prior to running any actions:

```c
// threads/init.c
int
main (void)
{
  ...
  thread_current()->cwd = dir_open_root();
  run_actions (argv);
  ...
}
```

Make child processes inherit parent’s current working directory:

```c
// threads/thread.c
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux)
{
  ...
  t->proc = p;
  list_push_back (&running_thread()->children, &p->elem);
  t->cwd = dir_reopen(&running_thread()->cwd); // call reopen to increment inode’s open_cnt, which is checked prior to `remove`
  ...
}
```

Helper method which splits a file system path into its components:

```c
/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
   next call will return the next file name part. Returns 1 if successful, 0 at
   end of string, -1 for a too-long file name part. */

static int
get_next_part (char part[NAME_MAX + 1], const char **srcp) {
    const char *src = *srcp;
    char *dst = part;
    /* Skip leading slashes. If it’s all slashes, we’re done. */
    while (*src == ’/’)
        src++;
    if (*src == ’\0’)
        return 0;
    /* Copy up to NAME_MAX character from SRC to DST. Add null terminator. */
    while (*src != ’/’ && *src != ’\0’) {
        if (dst < part + NAME_MAX)
            *dst++ = *src;
        else
            return -1;
        src++;
    }
    *dst = ’\0’;
    /* Advance source pointer. */
    *srcp = src;
    return 1;
}
```

Looks up an entire file path by recursively descending into subdirectories:

```c
/* Recursively looks up a file path
 * (including subdirectories) FILE_PATH, which can be either absolute (start with ‘/’) or relative.
 * Returns true if one exists, false otherwise.
 * On success, sets *INODE to an inode for the file,
 * otherwise to a null pointer.
 * The caller must close *INODE. */
bool
recursive_path_lookup (const char *file_path, struct inode *inode) {
    char *part = malloc(sizeof(char) * (NAME_MAX + 1));
    const char **srcp = &file_path;
    if (file_path[0] == ‘/’) // absolute path
        struct dir *dir = dir_open_root ();
    else // relative path
        struct dir *dir = thread_current()->cwd;
    bool exists;
    while (get_next_part(part, srcp)) {
        exists = dir_lookup(dir, part, &inode);
        if (!exists)
            return false;
        dir = dir_open(inode);
    }
    return true;
}
```

Modify `filesys_open`, `filesys_create`, and `filesys_remove` to take paths with subdirectories

```c
struct file *
filesys_{open,create,remove} (const char *name)
{
  struct inode *inode = recursive_path_lookup(name);
  struct dir *dir = dir_open(inode); // dir now refers to parent sub-directory
  ...
}
```

To differentiate between whether an `inode` represents a `file` or a `dir`, we add a boolean field:

```c
struct inode
  {
    bool is_dir; /* Boolean indicating if inode is directory (true) or file (false) */
  };
```

Instead of keeping track of `files` owned by a process (`t->file_list`), we now keep track of the `inode`s owned by a process and provide helpers for translating file descriptors into inodes:

```c

// threads/thread.h
struct thread {
  ...
  struct list inode_list;
  ...
}

// userprog/process.h
/* Associates fd with inode */
struct inode_data {
  int fd;
  struct inode *inode;
  struct list_elem elem;
};

// userprog/syscall.c
struct
inode_data *get_inode_data (int fd)
```


## Algorithms

We chose to use the following algorithms to implement the new syscalls:

* `bool chdir (const char *dir)`
  * `rewrite_to_abs_path` can be used to handle relative paths and “.” and “..” file names
  * Given the absolute path, modify `thread_current()->cwd`
  * Clean up by calling `dir_close` on the previous cwd (to decrement `open_cnt` which is checked before `remove`)
* `bool mkdir (const char *dir)`
  * `abs_path_lookup` modification to `filesys_{open,create,remove}` handles subdirectories and relative paths
  * With parent `struct dir *dir` set properly, provided implementation of `filesys_create` suffices.
* `bool readdir (int fd, char *name)`
  * `syscall_open` adds owned `inode`s to the current thread’s `t->inode_list`
  * `get_inode_data` looks up the `inode_data` associated with `fd`
  * `dir_oppen` opens the `dir`, which `dir_readdir` then reads from
* `bool isdir (int fd)`
  * After adding `is_dir` to `struct inode`, this can be accomplished by looking up the `inode` associated with `fd` using `get_inode_data` and checking the `is_dir` member.

To extend our existing syscalls to support subdirectories, we chose the following algorithms:

* `open`
  * `abs_path_lookup` modification to `filesys_{open,create,remove}` handles subdirectories and relative paths
  * Reuse `filesys/filesys.c`’s `filesys_open` but check `inode->is_dir` and call `dir_open` or `file_open` appropriately.
* `exec`
  * Addition of `struct dir* cwd` member to `struct thread` in `threads/thread.h` implements each process having an independent working directory
  * `threads/init.c` modification sets the initial processes working directory to the root directory
  * `threads/thread.c` modification implements child processes inheriting parent’s current working directory
* `remove`
  * We disallow deletion of a directory that is open by a process or in use as a process’s current working directory. To ensure this is the case, we can check whether `inode->open_cnt > 0` prior to a `remove`.
  * Check `inode->is_dir` to see if it’s a directory. Use previous logic if not, otherwise:
  * Use `dir_readdir` to see if the directory is empty.
  * Call `dir_remove` from parent directory on the child to remove it.
* `read` and `write`
  * Update those two system calls so they detects whether the given `file` is a directory using the ‘is_dir’ boolean field, and (1) either rejects request, or (2) delegate to `mkdir` and `readdir`.

## Synchronization

One potential synchronization issue arises when one thread might remove a
directory that’s open or the current directory of another thread. We disallow
this situation from occurring by checking `inode->open_cnt > 0` prior to
removals. In doing so, this means that we need to increment `open_cnt` not just
when a process acquires a descriptor, but also when it is the current directory
(and decrement when the current directory changes).

A synchronization need of any directory is that it should support multiple
writers and multiple readers running concurrently. On the I/O level, the disk
I/O is encapsulated by (task 1) buffer cache, so we want readers/writers to ask
(and wait) for various blocks from the buffer cache. Those readers/writes work
in concert under a conditional variable + consumer/producer model. Sometimes
the need of exclusive ownership of a directory arises (like during
creation/removal of directory). We can handle this by locking the underlying
inode.

Finally, another synchronization issue we were concerned about was serializing
operations only when necessary. Specifically, we should serialize only when two
operations access the same sub-tree in the directory hierarchy (and otherwise
allow operations to occur in parallel). To implement this, we will use the
`directory->parent` members in `struct dir* dir` to traverse the directory tree
up to the root, ensuring that no parent directory is open (i.e. locked). Then,
we will acquire a lock on the current file/directory before performing our
operation. This traversal is a critical section because we need to guarantee
that no thread acquires a lock on a directory we have previously checked during
the traversal until our operation is completed. Hence, we should disable
interrupts during this step.

## Rationale

Through abstracting files and directories both as `inode`s (differentiating
using `inode->is_dir` when required), we were able to re-use a lot of the
provided logic in the `filesys_{open,create,remove}` operations. Through
choosing to extend the provided implementations to handle file paths rather
than only file names relative to the root directory ‘/’, we are able to reuse
quite a bit of error handling and cleanup logic. However, in doing so we have
constrained our file names to not contain any ‘/’s. This constraint could be
worked around by escaping ‘/’s, which would be required anyways to prevent
ambiguity.

Many of the `filesys_*` methods delegate to `dir_*` methods, which perform
operations relative to a root directory. We realized that relative file paths
only needed to be resolved starting from the current directory rather than the
root, so storing the current directory in `thread_current()->cwd` allowed us to
directly use it as the first argument to `dir_{create,open,add}` methods.

We chose to differentiate between relative path and absolute path by looking at
the first char. If the first char is ‘/’, it’s an absolute path and we traverse
it using ‘recursive_path_lookup’. Else, if it’s a relative path, we begin our
traversal from `thread_current()->cwd`. In both cases, we check during each
step if the current path component is a valid directory, until we’ve reached
the end of our path.

We chose to disallow user processes from deleting the `cwd` of a running
process. Any removal of directories is enforced by ‘inode->open_cnt’. For
detail, see “Synchronization”.

To convert from a file descriptor, like 3, and locate the corresponding file or
directory `struct`, we chose to establish a linkage between the file descriptor
and inode (that could represent both file and directory) using these
aforementioned structs and methods:

```c
// in userprog/process.h
/* Associates fd with inode */
struct inode_data {
  int fd;
  struct inode *inode;
  struct list_elem elem;
};
// in userprog/syscall.c
struct
inode_data *get_inode_data (int fd)
```


# Additional Questions

__Please discuss a possible implementation strategy for write-behind and a
strategy for read-ahead__

We can implement write-behind by a simple pulse check through thread system’s
clock tick. In thread_tick(), add the functionality “flush dirty blocks to
filesystem block device from buffer cache when (tick % pulse) == 0” and “tick
!= 0”. This ensures that the buffer cache is regularly flushing data to a
persistent block device, even if there are no evictions or graceful shutdowns.

Read-ahead can be done in parallel with the original read. We use a daemon
thread for fetching the next blocks and and a consumer/producer model for
adding it to the buffer cache. Here’s the pseudocode, where `next` is a
function which predicts the block the system will need next (e.g. it could
simply predict the next sequential block, which would improve performance for
sequential file reads). We can thus save a disk read and kill cache_read_ahead
when next() reports that: the next sequential block is not in buffer and needs
to be fetched from disk.

```go
b=cache_get_block(sector_idx, inode);
cache_block_read(b);
cache_read_ahead(next(sector_idx));

func next(sector_idx) {
     /* next() predicts and grabs the next sector from buffer cache.
      * Signifies whether the next sector is in buffer cache.*/
}

Queue q;
func cache_read_ahead(sector s) {
        If (sector s is not in buffer) return;
        q.lock();
        q.add(request(s));
        q_cond.signal();
        q.unlock();
}

func cache_read_ahead_daemon() {
        while (1) {
                q.lock();
                while(q.empty()) {
                        q_cond.wait();
                }
                s=q.pop();
                q.unlock();
                Read from sector s;
        }
}
```
