#include "filesys/cache.h"
#include "filesys/inode.h"
#include "stdlib.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "filesys/rw-lock.h"
#include "string.h"
#include "threads/thread.h"
#include "stdio.h"
#include "devices/timer.h"
#include "lib/kernel/queue.h"


struct cache_block
{
  struct list_elem elem;        /* List element for buffer cache. */ 
  block_sector_t sector;        /* Corresponding sector number on disk. */ 
  bool dirty;                   /* Indicate modification since cached. */
  bool valid;                   /* Indicate cached status of the block. */
  void *data;                   /* 512 bytes of block data on disk. */

  struct rw_lock rw_lock;
};

struct list buffer_cache;
struct list free_cache;
struct lock cache_lock;
struct array_queue read_queue;


static bool block_init (struct cache_block *block);
static struct cache_block * evict_LRU_block (block_sector_t sector);
static struct cache_block * cache_lookup (block_sector_t sector);


static bool 
block_init (struct cache_block *block)
{
  ASSERT (block != NULL);
  block->sector = SIZE_MAX;
  block->dirty = false;
  block->valid = false;
  block->data = malloc (BLOCK_SECTOR_SIZE);
  if (!block->data)
    {
      free (block);
      return false;
    }

  rw_lock_init (&block->rw_lock);

  return true;
}

/* Initialize buffer cache and its lock. */
void
cache_init (void)
{
  lock_init (&cache_lock);
  list_init (&buffer_cache);
  list_init (&free_cache);
  queue_init (&read_queue, MAX_CACHE_SIZE, true);

  for (int n = 0; n < MAX_CACHE_SIZE; n++)
    {
      struct cache_block *block = malloc (sizeof (struct cache_block));
      if (block == NULL || !block_init (block))
        {
          printf ("Buffer cache: fail to allocate 64 cache blocks.");
          thread_exit ();
        }
      list_push_front (&free_cache, &block->elem);
    }
}

/* Reserve a block in buffer cache dedicated to hold this sector.
 * Possibly evicting some other unused buffer.
 * Either grant exclusive or shared access. */
struct cache_block * 
cache_get_block (block_sector_t sector, bool exclusive)
{
  struct cache_block *block = NULL;
  bool evicted;

  do
    {
      evicted = false;
      lock_acquire (&cache_lock);

      /* Checks if block was already cached. */
      block = cache_lookup (sector);

      /* The block was not cached. */
      if (block == NULL)
        {
          /* When cache is full, select Least recently used block. */
          if (list_empty (&free_cache))
            block = evict_LRU_block (sector);

          /* Otherwise, use available empty block. */
          else
            {
              struct list_elem *e = list_front (&free_cache);
              block = list_entry (e, struct cache_block, elem);
              block->sector = sector;
            }
        }
      
      /* At this point, block is cached in, empty, or invalid (evicted). */
      ASSERT (block != NULL);
      /* Mark the block as most recently accessed. */
      list_remove (&block->elem);
      list_push_front (&buffer_cache, &block->elem);
      
      lock_release (&cache_lock);
      
      /* Acquire per-block read-write lock. */
      if (exclusive)
        {
          write_lock_acquire (&block->rw_lock);
          block->rw_lock.mode = WRITE_LOCKED;
        }
      else
        {
          read_lock_acquire (&block->rw_lock);
          block->rw_lock.mode = READ_LOCKED;
        }

      /* Check if the block have been evicted
         while acquiring the read-write lock. */
      if (block->sector != sector)
        {
          evicted = true;
          block->rw_lock.mode = UNLOCKED;
          if (exclusive)
            write_lock_release (&block->rw_lock);
          else
            read_lock_release (&block->rw_lock);
        }

    /* Start over if block was indeed evicted. */
    } while (evicted);


  return block;
}

/* Locate the Least Recently Used block from the buffer cache. */
static struct cache_block * 
evict_LRU_block (block_sector_t sector)
{
  struct cache_block *victim = NULL;

  /* Iterate from the back of the buffer cache
     to find the Least recently used block that is not pinned. */
  while (!victim)
    {
      for (struct list_elem *e = list_rbegin (&buffer_cache); 
           e != list_rend (&buffer_cache); e = list_prev (e))
        {
          struct cache_block *block = list_entry (e, struct cache_block, elem);
          /* Check if the block is pinned by other process. 
             If not, select the block as victim. */
          if (write_lock_try_acquire (&block->rw_lock))
            {
              victim = block;
              break;
            }
        }
    }

  lock_release (&cache_lock);
  
  ASSERT (victim != NULL);
  /* Write back to disk if the victim is dirty. 
     Otherwise, non-dirty block can be simply discarded. */
  if (victim->dirty)
    block_write (fs_device, victim->sector, victim->data);

  /* Update the block's meta data before releasing the lock. */
  victim->sector = sector;
  victim->valid = false;
  victim->dirty = false;

  write_lock_release (&victim->rw_lock);

  lock_acquire (&cache_lock);

  return victim;
}

/* Release access to cache block. */
void
cache_put_block (struct cache_block *block)
{
  if (block->rw_lock.mode == WRITE_LOCKED)
    write_lock_release (&block->rw_lock);
  else if (block->rw_lock.mode == READ_LOCKED)
    read_lock_release (&block->rw_lock);
}

/* Read cache block from disk, returns pointer to data. */
void *
cache_read_block (struct cache_block *block)
{
  ASSERT (block != NULL);
  if (block->valid == false)
    {
      block_read (fs_device, block->sector, block->data);
      block->valid = true;
    }

  return block->data;
}

/* Fill cache block with zeros, returns pointer to data. */
void *
cache_zero_block (struct cache_block *block)
{
  ASSERT (block != NULL);
  memset (block->data, 0, BLOCK_SECTOR_SIZE);
  block->dirty = true;
  block->valid = true;
  return block->data;
}

/* Mark cache block dirty (must be written back). */
void
cache_mark_block_dirty (struct cache_block *block)
{
  ASSERT (block != NULL);
  block->dirty = true;
}

/* Search if the given sector was already cached into the buffer cache. */
static struct cache_block * 
cache_lookup (block_sector_t sector)
{
  struct cache_block *block = NULL;
  /* Iterate over the buffer cache and check if the requested block
     was already cached into the cache. */
  for (struct list_elem *e = list_begin (&buffer_cache); 
       e != list_end (&buffer_cache); e = list_next (e))
    {
      struct cache_block *curr_blk = list_entry (e, struct cache_block, elem);
      if (curr_blk->sector == sector)
        {
          block = curr_blk;
          break;
        }
    }

  return block;
}

/* Pre-fetch next block to be accessed into buffer cache before
   it's accessed. */
void cache_read_ahead (block_sector_t sector)
{
  /* Check if the next block to be accessed is valid. */
  if ((int32_t) sector < 0)
    return;

  block_sector_t *next_sector = malloc (sizeof (block_sector_t));
  if (next_sector == NULL)
    return;

  *next_sector = sector;
  queue_enqueue (&read_queue, next_sector);
}

/* Pre-fetch specified blocks from the queue upon signal.
   This should be done asynchronously in the background. */
void
cache_read_ahead_daemon (void *unused UNUSED)
{
  while (true)
    {
      block_sector_t *sector = (block_sector_t *) queue_dequeue (&read_queue);
     
      struct cache_block *block = cache_get_block (*sector, false);
      cache_read_block (block);
      cache_put_block (block);
      
      free (sector);
    }
}

/* Periodically write all dirty blocks inside buffer cache to disk. 
   This should be done asynchronously in the background. */
void
cache_write_behind_daemon (void *unused UNUSED)
{
  while (true)
    {
      timer_sleep (5000);
      cache_flush ();
    }
}

/* Write back all dirty blocks inside buffer cache to disk. */
void
cache_flush (void)
{
  for (struct list_elem *e = list_begin (&buffer_cache); 
       e != list_end (&buffer_cache); e = list_next (e)) 
    { 
      struct cache_block *block = list_entry (e, struct cache_block, elem);
      /* Write back only dirty block. */
      if (block->dirty) 
        {
          if (write_lock_try_acquire (&block->rw_lock))
            {
              /* Checks whether block has been evicted while acquiring the lock. */
              if (block->valid)
                {
                  block_write (fs_device, block->sector, block->data);
                  block->dirty = false;
                }
              write_lock_release (&block->rw_lock);
            }
        }
    }
}

