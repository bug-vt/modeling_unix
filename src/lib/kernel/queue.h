#ifndef __LIB_KERNEL_QUEUE_H
#define __LIB_KERNEL_QUEUE_H

#include "lib/stddef.h"
#include "lib/stdbool.h"
#include "threads/synch.h"

/* Our array queue is implemented by the use of the
   circular array. This was to avoid shifting items 
   every enqueue or dequeue operation. 
   To distinguish between empty and full queue, one
   spot in the queue must be empty at all time. The
   head will always point to that empty spot. Using
   this, the following assumptions can be made:
   (1) The queue is empty when the head and tail point 
       to the same spot in the queue 
   (2) The queue is full when the next spot after the
       tail would be head (including wrap around). */
struct array_queue
  {
    void **buffer;                  /* The circular array that hold data. */
    uint32_t capacity;              /* The true capacity + 1. */
    bool discard_mode;              /* Indicate to discard new element
                                       if the array is full. */
    uint32_t head;                  /* Font of the queue, 
                                       pointing right before the first element. */
    uint32_t tail;                  /* Back of the queue, 
                                       pointing at the last element. */
    struct lock lock;               /* Monitor lock. */
    struct condition items_avail;   /* Channel for signaling arrival of new element. */
    struct condition slots_avail;   /* Channel for signaling open spot. */
  };

void queue_init (struct array_queue *, size_t capacity, bool discard_mode);
bool queue_empty (struct array_queue *);
bool queue_full (struct array_queue *);
void *queue_peek (struct array_queue *);
void *queue_dequeue (struct array_queue *);
void queue_enqueue (struct array_queue *, void *);

#endif
