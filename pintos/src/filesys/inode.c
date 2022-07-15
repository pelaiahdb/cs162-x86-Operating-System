#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_BLOCK_CNT 122
#define INDIRECT_SECTOR_BLOCK_CNT 128

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk {
  block_sector_t start;               /* First data sector. */
  off_t length;                       /* File size in bytes. */
  unsigned magic;                     /* Magic number. */
  bool is_dir; // true if inode represents directory, false if file

  block_sector_t direct_blocks[DIRECT_BLOCK_CNT];
  block_sector_t indirect_block;
  block_sector_t doubly_indirect_block;
};

struct indirect_block_sector {
  block_sector_t blocks[INDIRECT_SECTOR_BLOCK_CNT];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock lock; /* Lock on inode operations. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos)
{
  ASSERT (inode != NULL);

  off_t idx, idx_i, idx_di;
  struct inode_disk *inode_disk = &inode->data;

  if (pos >= 0 && pos < inode->data.length) {
    idx = pos / BLOCK_SECTOR_SIZE;

    // direct
    off_t cnt = DIRECT_BLOCK_CNT;
    if (idx < cnt) {
      return inode_disk->direct_blocks[idx];
    }

    // indirect
    idx_i = idx - cnt;
    cnt += INDIRECT_SECTOR_BLOCK_CNT;
    if (idx < cnt) {
      block_sector_t sector;
      struct indirect_block_sector *indirect;
      indirect = calloc(1, sizeof(struct indirect_block_sector));

      cache_read (fs_device, inode_disk->indirect_block, indirect);
      sector = indirect->blocks[idx_i];

      free(indirect);
      return sector;
    }

    // doubly_indirect
    idx_i = ((idx - cnt) / INDIRECT_SECTOR_BLOCK_CNT);
    idx_di = ((idx - cnt) % INDIRECT_SECTOR_BLOCK_CNT);
    cnt += INDIRECT_SECTOR_BLOCK_CNT * INDIRECT_SECTOR_BLOCK_CNT;
    if (idx < cnt) {
      block_sector_t sector;
      struct indirect_block_sector *doubly_indirect;
      doubly_indirect = calloc(1, sizeof(struct indirect_block_sector));

      cache_read(fs_device, inode_disk->doubly_indirect_block, doubly_indirect);
      cache_read(fs_device, doubly_indirect->blocks[idx_i], doubly_indirect);
      sector = doubly_indirect->blocks[idx_di];

      free(doubly_indirect);
      return sector;
    }
  }

  // inode does not contain data for a byte at offset pos
  return -1;
}


/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void)
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device. The inode is a directory iff `is_dir` is true.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = is_dir;

      if (inode_allocate(disk_inode, disk_inode->length)) {
        cache_write(fs_device, sector, disk_inode);
        success = true;
      }

      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e))
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector)
        {
          inode_reopen (inode);
          return inode;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->lock);
  cache_read (fs_device, inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);

      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          free_map_release(inode->sector, 1);
          inode_deallocate(inode);
        }
      free (inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode)
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0)
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          cache_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   A write at end of file will extend the inode. */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset)
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  // extend if writing past EOF
  if (byte_to_sector(inode, offset + size) == (size_t) -1 ) {
    if (!inode_allocate(&inode->data, offset + size)) {
      return 0;
    }

    inode->data.length = offset + size;
    cache_write(fs_device, inode->sector, &inode->data);
  }

  while (size > 0)
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else
        {
          /* We need a bounce buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left)
            cache_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode)
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode)
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

int
inode_allocate (struct inode_disk *disk_inode, off_t length) {
  static char buffer[BLOCK_SECTOR_SIZE];
  size_t cnt = bytes_to_sectors(length);
  size_t c = MIN(cnt, DIRECT_BLOCK_CNT);

  // direct blocks
  size_t i;
  for (i = 0; i < c; i++) {
    if (disk_inode->direct_blocks[i] == 0) {
      if (!free_map_allocate(1, &disk_inode->direct_blocks[i])) {
        return 0;
      }
      cache_write(fs_device, disk_inode->direct_blocks[i], buffer);
    }
  }
  cnt -= c;

  // indirect
  if (cnt > 0) {
    c = MIN(cnt, INDIRECT_SECTOR_BLOCK_CNT);
    if (!inode_allocate_indirect(&disk_inode->indirect_block, c)) {
      return 0;
    }
    cnt -= c;
  }

  // doubly_indirect
  if (cnt > 0) {
    c = MIN(cnt, INDIRECT_SECTOR_BLOCK_CNT * INDIRECT_SECTOR_BLOCK_CNT);
    if (!inode_allocate_doubly_indirect(&disk_inode->doubly_indirect_block, c)) {
      return 0;
    }
    cnt -= c;
  }

  // something went wrong
  if (cnt > 0) {
    return 0;
  }

  return 1;
}

int
inode_allocate_indirect (block_sector_t* sectorp, size_t cnt) {
  static char buffer[BLOCK_SECTOR_SIZE];
  struct indirect_block_sector indirect;

  if (*sectorp == 0) {
    free_map_allocate(1, sectorp);
    cache_write(fs_device, *sectorp, buffer);
  }
  cache_read(fs_device, *sectorp, &indirect);

  size_t i;
  for (i = 0; i < cnt; i++) {
    if (indirect.blocks[i] == 0) {
      if (!free_map_allocate(1, &indirect.blocks[i])) {
        return 0;
      }
      cache_write(fs_device, indirect.blocks[i], buffer);
    }
  }

  cache_write(fs_device, *sectorp, &indirect);
  return 1;
}

int
inode_allocate_doubly_indirect (block_sector_t* sectorp, size_t cnt) {
  static char buffer[BLOCK_SECTOR_SIZE];
  struct indirect_block_sector doubly_indirect;

  if (*sectorp == 0) {
    free_map_allocate(1, sectorp);
    cache_write(fs_device, *sectorp, buffer);
  }
  cache_read(fs_device, *sectorp, &doubly_indirect);

  size_t i;
  for (i = 0; i <= DIV_ROUND_UP(cnt, INDIRECT_SECTOR_BLOCK_CNT); i++) {
    size_t c = MIN(cnt, INDIRECT_SECTOR_BLOCK_CNT); // check remaining cnt
    if (!inode_allocate_indirect(&doubly_indirect.blocks[i], c)) {
      return 0;
    }
    cnt -= c;
  }

  cache_write(fs_device, *sectorp, &doubly_indirect);
  return 1;
}

void
inode_deallocate (struct inode *inode) {
  size_t cnt = bytes_to_sectors(inode->data.length);
  size_t c = MIN(cnt, DIRECT_BLOCK_CNT);

  // direct
  size_t i;
  for (i = 0; i < c; i++) {
    free_map_release(inode->data.direct_blocks[i], 1);
  }
  cnt -= c;

  // indirect
  if (c > 0) {
    c = MIN(cnt, INDIRECT_SECTOR_BLOCK_CNT);
    inode_deallocate_indirect(inode->data.indirect_block, c);
    cnt -= c;
  }

  // doubly_indirect
  if (c > 0) {
    c = MIN(cnt, INDIRECT_SECTOR_BLOCK_CNT * INDIRECT_SECTOR_BLOCK_CNT);
    inode_deallocate_doubly_indirect (inode->data.doubly_indirect_block, c);
    cnt -= c;
  }
}

void
inode_deallocate_indirect (block_sector_t sector, size_t cnt) {
  struct indirect_block_sector indirect;
  cache_read(fs_device, sector, &indirect);

  size_t i;
  for (i = 0; i < cnt; i++) {
    free_map_release(indirect.blocks[i], 1);
    cnt -= 1;
  }

  free_map_release(sector, 1);
}

void
inode_deallocate_doubly_indirect (block_sector_t sector, size_t cnt) {
  struct indirect_block_sector doubly_indirect;
  cache_read(fs_device, sector, &doubly_indirect);

  size_t i;
  for (i = 0; i <= DIV_ROUND_UP(cnt, INDIRECT_SECTOR_BLOCK_CNT); i++) {
    size_t c = MIN(cnt, INDIRECT_SECTOR_BLOCK_CNT); // check remaining cnt
    inode_deallocate_indirect(doubly_indirect.blocks[i], c);
    cnt -= c;
  }

  free_map_release(sector, 1);
}

/* Project 3-3 helper func. */
bool
inode_is_dir(const struct inode *inode) {
  return inode->data.is_dir;
}

bool inode_is_removed (const struct inode * inode) {
  return inode->removed;
}

int inode_open_cnt (const struct inode * inode) {
  return inode->open_cnt;
}

void inode_acquire (struct inode * inode) {
  lock_acquire(&inode->lock);
}

void inode_release (struct inode * inode) {
  lock_release(&inode->lock);
}
