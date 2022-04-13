#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "stdio.h"

#define NUM_DIRECT 123
#define NUM_INDIRECT 128
#define NUM_DOUBLE_INDIRECT 16384

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44


/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    int is_dir;                         /* File type.
                                           0 = File
                                           1 = Directory */
    unsigned magic;                     /* Magic number. */
    block_sector_t direct[NUM_DIRECT];
    block_sector_t indirect;
    block_sector_t double_indirect;
  };

static block_sector_t lookup_direct_table (struct inode_disk *, int, bool);
static block_sector_t lookup_indirect_table (struct inode_disk *, int, bool);
static block_sector_t lookup_double_indirect_table (struct inode_disk *, int, bool);
static block_sector_t access_indirect_block (block_sector_t, bool);

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
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos, bool write) 
{
  ASSERT (inode != NULL);
  struct cache_block *block = cache_get_block (inode->sector, write);
  struct inode_disk *data = (struct inode_disk *) cache_read_block (block);

  block_sector_t sector;
  /* Determine the sector mapping between given position and
     multi-level indices. */
  int mapping = pos / BLOCK_SECTOR_SIZE; 
  /* If position is less than 62 KB, then it must be
     located inside one of the direct blocks. */
  if (mapping < NUM_DIRECT)
    sector = lookup_direct_table (data, mapping, write);
  /* Else, if position is less than 126 KB, then it must be
     located inside the indirect block. */
  else if (mapping < NUM_DIRECT + NUM_INDIRECT)
    sector = lookup_indirect_table (data, mapping, write);
  /* Else, if position is less than 8.12 MB, then it must be
     located inside the doubly indirect block. */
  else if (mapping < NUM_DIRECT + NUM_INDIRECT + NUM_DOUBLE_INDIRECT)
    sector = lookup_double_indirect_table (data, mapping, write);
  /* Otherwise, sector mapping (thus given position) is invalid. */
  else
    sector = -1;

  /* Update inode block when new sector got added. */
  if ((int32_t) sector != -1 && (int32_t) sector != -2)
    cache_mark_block_dirty (block);

  cache_put_block (block);

  return sector;
}

/* Find the corresponding sector in sparse file 
   using the given sector mapping in the sets of direct blocks. */
static block_sector_t 
lookup_direct_table (struct inode_disk *data, int mapping, bool write)
{
  /* Locate the direct block in direct table. */
  block_sector_t sector = data->direct[mapping];
  /* Prefetch next sector */
  if (mapping < NUM_DIRECT - 1)
    cache_read_ahead (data->direct[mapping + 1]);
  /* Checks if reading pass the EOF or hole in sparse file. */
  if (!write && (int32_t) sector == -1)
    return -1;
  /* Otherwise, 
     (1) writing pass the EOF extend the file. 
     (2) writing to the hole in sparse file fill in the hole with new sector. */
  else if (write && (int32_t) sector == -1)
    {
      if (!free_map_allocate (1, &data->direct[mapping]))
        return -2;  /* Run out of space in free map. */

      sector = data->direct[mapping];
    }

  return sector;
}

/* Find the corresponding sector in sparse file 
   using the given sector mapping in the indirect block. */
static block_sector_t
lookup_indirect_table (struct inode_disk *data, int mapping, bool write)
{
  /* First access the indirect block. */
  block_sector_t indirect = access_indirect_block (data->indirect, write);
  if ((int32_t) indirect == -1 || (int32_t) indirect == -2)
    return indirect;

  data->indirect = indirect;
  /* From indirect table, locate direct block. */
  struct cache_block *indirect_block = cache_get_block (indirect, write);
  block_sector_t *indirect_table = (block_sector_t *) cache_read_block (indirect_block);
  int index = mapping - NUM_DIRECT;
  block_sector_t sector = indirect_table[index];    
  /* Prefetch next sector */
  if (index < NUM_INDIRECT - 1)
    cache_read_ahead (indirect_table[index + 1]);
  /* Checks if reading pass the EOF or hole in sparse file. */
  if (!write && (int32_t) sector == -1)
    {
      cache_put_block (indirect_block);
      return -1;
    }
  /* Otherwise, 
     (1) writing pass the EOF extend the file. 
     (2) writing to the hole in sparse file fill in the hole with new sector. */
  else if (write && (int32_t) sector == -1)
    {
      if (!free_map_allocate (1, &indirect_table[mapping - NUM_DIRECT]))
        {
          cache_put_block (indirect_block);
          return -2;  /* Run out of space in free map. */
        }
      sector = indirect_table[mapping - NUM_DIRECT];
      cache_mark_block_dirty (indirect_block);
    }

  cache_put_block (indirect_block);

  return sector;
}

/* Find the corresponding sector in sparse file 
   using the given sector mapping in the doubly indirect block. */
static block_sector_t
lookup_double_indirect_table (struct inode_disk *data, int mapping, bool write)
{
  /* First access the doubly indirect block. */
  block_sector_t double_indirect = access_indirect_block (data->double_indirect, write);
  if ((int32_t) double_indirect == -1 || (int32_t) double_indirect == -2)
    return double_indirect;

  data->double_indirect = double_indirect;
  struct cache_block *double_indirect_block = cache_get_block (double_indirect, write);
  block_sector_t *double_indirect_table 
      = (block_sector_t *) cache_read_block (double_indirect_block);

  /* From doubly indirect table, locate indirect table index. */
  int index = (mapping - NUM_DIRECT - NUM_INDIRECT) / NUM_INDIRECT;
  block_sector_t indirect = access_indirect_block (double_indirect_table[index], write);
  if ((int32_t) indirect == -1 || (int32_t) indirect == -2)
    {
      cache_put_block (double_indirect_block);
      return indirect;
    }
  /* New indirect sector was allocated to doubly indirect block. */
  if (double_indirect_table[index] != indirect)
    {
      double_indirect_table[index] = indirect;
      cache_mark_block_dirty (double_indirect_block);
    }

  cache_put_block (double_indirect_block);

  /* From indirect table, locate direct block. */
  struct cache_block *indirect_block = cache_get_block (indirect, write);
  block_sector_t *indirect_table = (block_sector_t *) cache_read_block (indirect_block);
  index = (mapping - NUM_DIRECT - NUM_INDIRECT) % NUM_INDIRECT;
  block_sector_t sector = indirect_table[index];    
  /* Prefetch next sector */
  if (index < NUM_INDIRECT - 1)
    cache_read_ahead (indirect_table[index + 1]);
  /* Checks if reading pass the EOF or hole in sparse file. */
  if (!write && (int32_t) sector == -1)
    {
      cache_put_block (indirect_block);
      return -1;
    }
  /* Otherwise, 
     (1) writing pass the EOF extend the file. 
     (2) writing to the hole in sparse file fill in the hole with new sector. */
  else if (write && (int32_t) sector == -1)
    {
      if (!free_map_allocate (1, &indirect_table[index]))
        {
          cache_put_block (indirect_block);
          return -2;  /* Run out of space in free map. */
        }

      sector = indirect_table[index];
      cache_mark_block_dirty (indirect_block);
    }
  cache_put_block (indirect_block);

  return sector;
}

/* Access the given indirect block.
   If empty and accessed while writing, 
   then allocate new indirect block that can hold 128 direct block. */
static block_sector_t
access_indirect_block (block_sector_t indirect, bool write)
{
  /* Checks if reading pass the EOF or hole in sparse file. */
  if (!write && (int32_t) indirect == -1)
    return -1;
  /* If write and block is empty, then allocate new indirect block (table). */
  else if (write && (int32_t) indirect == -1)
    {
      if (!free_map_allocate (1, &indirect))
        return -2;  /* Run out of space in free map. */

      /* Initialize all direct blocks within the indirect table to -1. */
      struct cache_block *block = cache_get_block (indirect, true);
      int32_t *indirect_table = cache_zero_block (block);
      for (int i = 0; i < NUM_INDIRECT; i++)
        indirect_table[i] = -1;
      cache_put_block (block);
    }

  return indirect;
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
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, int is_dir)
{
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof (struct inode_disk) == BLOCK_SECTOR_SIZE);

  struct cache_block *block = cache_get_block (sector, true);
  struct inode_disk *disk_inode = (struct inode_disk *) cache_zero_block (block);
  for (int i = 0; i < NUM_DIRECT; i++)
    disk_inode->direct[i] = -1;

  disk_inode->indirect = -1;
  disk_inode->double_indirect = -1;
  disk_inode->length = length;
  disk_inode->is_dir = is_dir;
  cache_put_block (block);

  if (length > 0)
    {
      static char zero[1];
      struct inode *inode = inode_open (sector);
      if (!inode)
        {
          inode_remove (inode);
          inode_close (inode);
          goto create_done;
        }
      /* Extend the file length from 0 to length by writing at the end. */
      bool written = inode_write_at (inode, zero, 1, length - 1) == 1;
      if (!written)
        {
          inode_remove (inode);
          inode_close (inode);
          goto create_done;
        }
      inode_close (inode);
    }
  success = true;

create_done:
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
  struct cache_block *block = cache_get_block (inode->sector, false);
  cache_read_block (block);
  cache_put_block (block);
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
          /* Un-mark all data blocks associated with given inode. */
          for (off_t pos = 0; pos < inode_length(inode); pos += BLOCK_SECTOR_SIZE)
            {
              block_sector_t sector = byte_to_sector (inode, pos, false);
              if ((int32_t) sector != -1)
                free_map_release (sector, 1); 
            }

          struct cache_block *block = cache_get_block (inode->sector, true); 
          struct inode_disk *data = (struct inode_disk *) cache_read_block (block);
        
          /* Un-mark indirect block. */
          if ((int32_t) data->indirect != -1)
            free_map_release (data->indirect, 1);
          /* Un-mark doubly indirect block. */
          if ((int32_t) data->double_indirect != -1)
            {
              struct cache_block *double_indirect_block 
                  = cache_get_block (data->double_indirect, true); 
              block_sector_t *double_indirect_table 
                  = (block_sector_t *) cache_read_block (double_indirect_block);
              /* Un-mark all non-empty indirect blocks 
                 inside doubly indirect block. */
              for (int i = 0; i < NUM_INDIRECT; i++)
                {
                  if ((int32_t) double_indirect_table[i] != -1)
                    free_map_release (double_indirect_table[i], 1); 
                }
              cache_put_block (double_indirect_block);
              free_map_release (data->double_indirect, 1); 
            }

          cache_put_block (block);
          /* Un-mark block that hold inode. */
          free_map_release (inode->sector, 1);
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
  off_t length = inode_length (inode);

  /* Check if reading pass EOF. */
  if (offset > length)
    return 0;
 
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, false);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = length - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      /* Read from empty sector in sparse file. */
      if ((int32_t) sector_idx == -1)
        memset (buffer + bytes_read, 0, chunk_size);
      /* Otherwise, read data from the data block. */
      else
        {
          struct cache_block *block = cache_get_block (sector_idx, false);
          void *data = cache_read_block (block);
          if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
            {
              /* Read full sector into caller's buffer. */
              memcpy (buffer + bytes_read, data, chunk_size);
            }
          else 
            {
              /* Read partial sector into caller's buffer. */
              memcpy (buffer + bytes_read, data + sector_ofs, chunk_size);
            }
          cache_put_block (block);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, true);
      if ((int32_t) sector_idx == -2)
        break;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = size < sector_left ? size : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      struct cache_block *block = cache_get_block (sector_idx, true);
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Avoid unnecessary (possible) disk access. */
          void *data = cache_zero_block (block);
          /* Write full sector to cache. */
          memcpy (data, buffer + bytes_written, chunk_size);
        }
      else 
        {
          void *data = cache_read_block (block);
          /* Write partial sector to cache. */
          memcpy (data + sector_ofs, buffer + bytes_written, chunk_size);
        }

      cache_mark_block_dirty (block);
      cache_put_block (block);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  /* Update the length of the file if extended. */
  struct cache_block *block = cache_get_block (inode->sector, true);
  struct inode_disk *data = (struct inode_disk *) cache_read_block (block);
  if (data->length < offset)
    {
      data->length = offset;
      cache_mark_block_dirty (block);
    }
  cache_put_block (block);

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
  struct cache_block *block = cache_get_block (inode->sector, false);
  struct inode_disk *data = (struct inode_disk *) cache_read_block (block);
  off_t length = data->length;
  cache_put_block (block);
  return length;
}

/* Checks if the given inode is a directory. */
bool
inode_is_dir (const struct inode *inode)
{
  struct cache_block *block = cache_get_block (inode->sector, false);
  struct inode_disk *data = (struct inode_disk *) cache_read_block (block);
  int is_dir = data->is_dir;
  cache_put_block (block);
  return is_dir == 1;
}

/* Check if the given inode have been removed. */
bool
inode_is_removed (const struct inode *inode)
{
  return inode->removed;
}
