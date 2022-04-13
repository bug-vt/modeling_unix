#include "filesys/rw-lock.h"
#include "threads/synch.h"
#include "filesys/cache.h"


void
rw_lock_init (struct rw_lock *rw_lock)
{
  rw_lock->num_readers = 0;
  rw_lock->num_writers = 0;
  lock_init (&rw_lock->monitor_lock);
  cond_init (&rw_lock->can_read);
  cond_init (&rw_lock->can_write);
  rw_lock->pending_readers = 0;
  rw_lock->pending_writer = false;
  rw_lock->reader_next = false;
  rw_lock->mode = UNLOCKED;
}

/* Acquire inclusive (shared) access to the lock.
   That is, other readers can access the lock concurrently while
   the current reader hold the lock. However, writers must wait
   for their turns. */
void 
read_lock_acquire (struct rw_lock *rw_lock)
{
  lock_acquire (&rw_lock->monitor_lock);

  /* If a writer is holding a lock or if there are pending writers,
     then put the reader into a pending list. 
     On the other hand, if one of the writer woke them up, then
     it must be reader's turn to hold the lock. */
  while ((rw_lock->num_writers > 0 || rw_lock->pending_writer) 
         && !rw_lock->reader_next)
    {
      rw_lock->pending_readers++;
      cond_wait (&rw_lock->can_read, &rw_lock->monitor_lock);
      rw_lock->pending_readers--;
    }

  rw_lock->num_readers++;

  lock_release (&rw_lock->monitor_lock);
}


/* Release the shared access to the lock for the current reader. 
   Other readers still might be holding the lock. */
void 
read_lock_release (struct rw_lock *rw_lock)
{
  lock_acquire (&rw_lock->monitor_lock);

  rw_lock->num_readers--;
  rw_lock->reader_next = false;
  /* Wake up one of the writers if any. The followings are possible:
     (1) The last reader: 
         A writer will take turn next.
     (2) Not the last reader:
         A woken up writer will go back to sleep, but notify readers that
         it is pending, making next request from reader to go to pending list.*/
  cond_signal (&rw_lock->can_write, &rw_lock->monitor_lock);

  lock_release (&rw_lock->monitor_lock);
}


/* Acquire exclusive access to the lock.
   That is, other readers or writers will be blocked upon request 
   until the exclusive access gets released. */
void 
write_lock_acquire (struct rw_lock *rw_lock)
{
  lock_acquire (&rw_lock->monitor_lock);

  /* Checks if any readers or another writer is already holding the lock.
     Also, checks if it is reader's turn to hold the lock.
     In any of those cases, put the writer into a pending list. */
  while (rw_lock->num_readers > 0 || rw_lock->num_writers > 0 
         || rw_lock->reader_next)
    {
      rw_lock->pending_writer = true;
      cond_wait (&rw_lock->can_write, &rw_lock->monitor_lock);
      rw_lock->pending_writer = false;
    }

  /* At this point, only one writer should be holding the
     read-write lock. */
  rw_lock->num_writers++;
  ASSERT (rw_lock->num_writers == 1);

  lock_release (&rw_lock->monitor_lock);
}


/* Checks if the read-write lock can be acquired exclusively.
   Return false immediately if readers or another writer is
   already holding a lock.
   Otherwise, acquire an exclusive lock and return true. */
bool
write_lock_try_acquire (struct rw_lock *rw_lock)
{
  lock_acquire (&rw_lock->monitor_lock);

  /* Checks if any readers or another writer is already holding the lock. */
  if (rw_lock->num_readers > 0 || rw_lock->num_writers > 0)
    {
      lock_release (&rw_lock->monitor_lock);
      return false;
    }

  rw_lock->num_writers++;

  lock_release (&rw_lock->monitor_lock);
  return true;
}


/* Release exclusive access to the lock. */
void 
write_lock_release (struct rw_lock *rw_lock)
{
  lock_acquire (&rw_lock->monitor_lock);

  rw_lock->num_writers--;
  ASSERT (rw_lock->num_writers == 0);

  /* If there are no readers, then wake up a pending writer if any. */
  if (rw_lock->pending_readers == 0)
    cond_signal (&rw_lock->can_write, &rw_lock->monitor_lock);
  /* Otherwise, wake up all pending readers. */
  else
    {
      /* Make sure next reader would go next. */
      rw_lock->reader_next = true;
      cond_broadcast (&rw_lock->can_read, &rw_lock->monitor_lock);
    }

  lock_release (&rw_lock->monitor_lock);
}
