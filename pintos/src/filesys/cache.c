#include <list.h>
#include <debug.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Maximum number of cache blocks */
#define CACHE_SIZE 64

static int disk_reads;
static int disk_writes;

/* Buffer cache block */
struct cache_block {
    struct list_elem elem;
    block_sector_t disk_block;
    uint8_t memory_block[BLOCK_SECTOR_SIZE];
    bool recently_used;
    bool dirty_bit;
};

/* Single lock for serialized cache access */
static struct lock cache_lock;

/* The main buffer cache as a list */
static struct list buffer_cache;

void
cache_read (struct block *block, block_sector_t sector, void *buffer)
{
  lock_acquire (&cache_lock);
  if (list_size (&buffer_cache) < CACHE_SIZE)
    {
      struct list_elem *e;
      for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache);
           e = list_next (e))
        {
          struct cache_block *t = list_entry (e, struct cache_block, elem);
          if (t->disk_block == sector)
            {
              memcpy (buffer, t->memory_block, BLOCK_SECTOR_SIZE);
              t->recently_used = true;
              lock_release (&cache_lock);
              return;
            }
        }
      // Cache not full, so on page fault:
      struct cache_block *new_entry = malloc (sizeof (struct cache_block));
      block_read (block, sector, new_entry->memory_block);
      disk_reads++;
      new_entry->recently_used = true;
      new_entry->dirty_bit = false;
      new_entry->disk_block = sector;
      list_push_back (&buffer_cache, &new_entry->elem);
      memcpy (buffer, new_entry->memory_block, BLOCK_SECTOR_SIZE);
      lock_release (&cache_lock);
      return;
    }
  else // Cache is full, so needs eviction on page fault
    {
      struct list_elem *e;
      for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache);
           e = list_next (e))
        {
          struct cache_block *t = list_entry (e, struct cache_block, elem);
          if (t->disk_block == sector)
            {
              memcpy (buffer, t->memory_block, BLOCK_SECTOR_SIZE);
              t->recently_used = true;
              lock_release (&cache_lock);
              return;
            }
        }
      // Needs clock eviction on page fault
      while (true)
        {
          for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache);
             e = list_next (e))
            {
              struct cache_block *t = list_entry (e, struct cache_block, elem);
              if (!t->recently_used)
                {
                  if (t->dirty_bit)
                    {
                      block_write (block, t->disk_block, t->memory_block);
                      disk_writes++;
                    }
                  t->disk_block = sector;
                  t->recently_used = true;
                  t->dirty_bit = false;
                  block_read (block, sector, t->memory_block);
                  disk_reads++;
                  memcpy (buffer, t->memory_block, BLOCK_SECTOR_SIZE);
                  lock_release (&cache_lock);
                  return;
                }
              else
                {
                  t->recently_used = false;
                }
            }
        }
    }
}

void
cache_write (struct block *block, block_sector_t sector, const void *buffer)
{
  lock_acquire (&cache_lock);
  if (list_size (&buffer_cache) < CACHE_SIZE)
    {
      struct list_elem *e;
      for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache);
           e = list_next (e))
        {
          struct cache_block *t = list_entry (e, struct cache_block, elem);
          if (t->disk_block == sector)
            {
              memcpy (t->memory_block, buffer, BLOCK_SECTOR_SIZE);
              t->recently_used = true;
              t->dirty_bit = true;
              lock_release (&cache_lock);
              return;
            }
        }
      // Cache not full, so on page fault:
      struct cache_block *new_entry = malloc (sizeof (struct cache_block));
      new_entry->disk_block = sector;
      new_entry->recently_used = true;
      new_entry->dirty_bit = true;
      memcpy (new_entry->memory_block, buffer, BLOCK_SECTOR_SIZE);
      list_push_back (&buffer_cache, &new_entry->elem);
      lock_release (&cache_lock);
      return;
    }
  else // Cache is full, so needs eviction on page fault
    {
      struct list_elem *e;
      for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache);
           e = list_next (e))
        {
          struct cache_block *t = list_entry (e, struct cache_block, elem);
          if (t->disk_block == sector)
            {
              memcpy (t->memory_block, buffer, BLOCK_SECTOR_SIZE);
              t->recently_used = true;
              t->dirty_bit = true;
              lock_release (&cache_lock);
              return;
            }
        }
      // Needs clock eviction on page fault
      while (true)
        {
          for (e = list_begin (&buffer_cache); e != list_end (&buffer_cache);
             e = list_next (e))
            {
              struct cache_block *t = list_entry (e, struct cache_block, elem);
              if (!t->recently_used)
                {
                  if (t->dirty_bit)
                    {
                      block_write (block, t->disk_block, t->memory_block);
                      disk_writes++;
                    }
                  t->disk_block = sector;
                  t->recently_used = true;
                  t->dirty_bit = true;
                  memcpy (t->memory_block, buffer, BLOCK_SECTOR_SIZE);
                  lock_release (&cache_lock);
                  return;
                }
              else
                {
                  t->recently_used = false;
                }
            }
        }
    }
}

void
cache_done (void)
{
  lock_acquire (&cache_lock);
  struct list_elem *e;
  while (!list_empty(&buffer_cache))
    {
      e = list_pop_back(&buffer_cache);
      struct cache_block *t = list_entry (e, struct cache_block, elem);
      if (t->dirty_bit)
        {
          block_write (fs_device, t->disk_block, t->memory_block);
          disk_writes++;
        }
      free(t);
    }
  lock_release (&cache_lock);
}

void
cache_init (void)
{
  disk_reads = 0;
  disk_writes = 0;
  lock_init (&cache_lock);
  list_init (&buffer_cache);
}

int
p_diskreads (void)
{
  return disk_reads;
}

int
p_diskwrites (void)
{
  return disk_writes;
}

void
cache_reset (void)
{
  cache_done ();
}