#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H


#include "threads/synch.h"
#include "devices/block.h"

#define MAX_CACHE_SIZE 64

struct list buffer_cache;
struct list free_cache;
struct cache_block;


void cache_init (void);
struct cache_block * cache_get_block (block_sector_t sector, bool exclusive);
void cache_put_block (struct cache_block *);
void *cache_read_block (struct cache_block *);
void *cache_zero_block (struct cache_block *);
void cache_mark_block_dirty (struct cache_block *);
// void cache_read_ahead (sector);
void cache_flush (void);

#endif  /* filesys/cache.h */
