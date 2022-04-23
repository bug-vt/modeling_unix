#ifndef FILESYS_PIPE_H
#define FILESYS_PIPE_H

#include "lib/stddef.h"
#include "lib/stdbool.h"
#include "threads/synch.h"
#include "filesys/file.h"

/* Our pipe is implemented by the use of the
   ring buffer. This was to avoid shifting items 
   every enqueue or dequeue operation. 
   To distinguish between empty and full buffer, one
   spot in the buffer must be empty at all time. The
   head will always point to that empty spot. Using
   this, the following assumptions can be made:
   (1) The buffer is empty when the head and tail point 
       to the same spot in the buffer.
   (2) The buffer is full when the next spot after the
       tail would be head (including wrap around). */
struct pipe 
  {
    uint8_t *buffer;               /* The ring buffer that hold data. */
    uint32_t capacity;              /* The true capacity + 1. */
    uint32_t head;                  /* Front of the queue, 
                                       pointing right before the first element. */
    uint32_t tail;                  /* Back of the queue, 
                                       pointing at the last element. */
    struct lock lock;               /* Monitor lock. */
    struct condition items_avail;   /* Channel for signaling arrival of new element. */
    struct condition slots_avail;   /* Channel for signaling open spot. */
    struct file *read_end;
    struct file *write_end;
  };

struct pipe *pipe_create (size_t);
bool pipe_open (struct file **read_end, struct file **write_end);
void pipe_close (struct pipe *, struct file *);
int pipe_read (struct pipe *pipe, void *buffer_, off_t size);
int pipe_write (struct pipe *pipe, const void *buffer_, off_t size);

#endif /* filesys/pipe.h */
