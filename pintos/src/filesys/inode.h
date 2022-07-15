#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <stdlib.h>
#include "filesys/off_t.h"
#include "devices/block.h"

struct bitmap;
struct inode;
struct inode_disk;

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

int inode_allocate_indirect (block_sector_t* sectorp, size_t cnt);
int inode_allocate_doubly_indirect (block_sector_t* sectorp, size_t cnt);
int inode_allocate (struct inode_disk *disk_inode, off_t length);
void inode_deallocate_indirect (block_sector_t sector, size_t cnt);
void inode_deallocate_doubly_indirect (block_sector_t sector, size_t cnt);
void inode_deallocate (struct inode *inode);
bool inode_is_dir (const struct inode *);

/* Project 3-3 helper func. */
bool inode_is_directory (const struct inode *);
bool inode_is_removed (const struct inode *);
int inode_open_cnt (const struct inode *);

#endif /* filesys/inode.h */
