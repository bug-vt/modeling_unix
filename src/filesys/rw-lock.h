#ifndef FILESYS_RW_LOCK_H
#define FILESYS_RW_LOCK_H

#include "stdlib.h"
#include "filesys/cache.h"

enum _rw_mode { UNLOCKED, READ_LOCKED, WRITE_LOCKED };
typedef enum _rw_mode rw_mode;

struct rw_lock
{
  int num_readers;              /* Number of readers on the block. */
  int num_writers;              /* Number of writers on the block. */
  struct lock monitor_lock;     /* Monitor lock for read-write lock. */
  struct condition can_read;    /* Channel for signaling pending readers. */
  struct condition can_write;   /* Channel for signaling pending writers. */
  int pending_readers;          /* Number of pending readers. */
  bool pending_writer;          /* Indicate present of pending writers. */
  rw_mode mode;                 /* Read-write lock status. */
};

void rw_lock_init (struct rw_lock *);
void read_lock_acquire (struct rw_lock *);
void read_lock_release (struct rw_lock *);
void write_lock_acquire (struct rw_lock *);
bool write_lock_try_acquire (struct rw_lock *);
void write_lock_release (struct rw_lock *);


#endif
