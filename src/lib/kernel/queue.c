#include "queue.h"
#include "threads/thread.h"
#include <stdio.h>
#include "threads/malloc.h"

/* Initialize the array queue. */
void
queue_init (struct array_queue *queue, size_t capacity, bool discard_mode)
{
  ASSERT (queue != NULL);

  /* Since one spot in the queue must be empty, 
     add one more spot from the true capacity. */
  queue->capacity = capacity + 1;
  queue->discard_mode = discard_mode;
  queue->head = 0;
  queue->tail = 0;
  lock_init (&queue->lock);
  cond_init (&queue->items_avail);
  cond_init (&queue->slots_avail);
  queue->buffer = malloc ((capacity + 1) * sizeof (uint32_t));
  if (queue->buffer == NULL)
    {
      printf ("Array queue: fail to reserve %d elements space in kernel.", capacity);
      free (queue);
      thread_exit ();
    }
}

/* Return true if the queue is empty. False otherwise. */
bool 
queue_empty (struct array_queue *queue)
{
  ASSERT (queue != NULL);
  return queue->head == queue->tail;
}

/* Return true if the queue is full. False otherwise. */
bool 
queue_full (struct array_queue *queue)
{
  ASSERT (queue != NULL);
  return queue->head == (queue->tail + 1) % queue->capacity;
}

/* Return the front most element in the queue. */
uint32_t
queue_peek (struct array_queue *queue)
{
  ASSERT (queue != NULL);
  uint32_t item = -1;
  lock_acquire (&queue->lock);

  if (!queue_empty (queue))
    item = queue->buffer[(queue->head + 1) % queue->capacity];

  lock_release (&queue->lock);

  return item;
}

/* Pop the element from the front of the queue. */
uint32_t
queue_dequeue (struct array_queue *queue)
{
  ASSERT (queue != NULL);
  lock_acquire (&queue->lock);
  /* Wait if queue is empty. */
  while (queue_empty (queue))
    cond_wait (&queue->items_avail, &queue->lock);

  /* Move the head to point to front element. */
  queue->head = (queue->head + 1) % queue->capacity;
  uint32_t item = queue->buffer[queue->head];
  /* Notify that there is at least one free spot inside the queue. */
  cond_signal (&queue->slots_avail, &queue->lock);

  lock_release (&queue->lock);
  return item;
}

/* Push the given element to the back of the queue. */
void
queue_enqueue (struct array_queue *queue, uint32_t item)
{
  ASSERT (queue != NULL);
  lock_acquire (&queue->lock);
  /* Discard new item when the queue is full in discard mode. */
  if (queue->discard_mode && queue_full (queue))
    {
      lock_release (&queue->lock);
      return;
    }
  /* Otherwise, wait if queue is full. */
  while (queue_full (queue))
    cond_wait (&queue->slots_avail, &queue->lock);

  /* Move the tail to point to empty spot follow by the last element. */
  queue->tail = (queue->tail + 1) % queue->capacity;
  queue->buffer[queue->tail] = item;
  /* Notify that there is at least one element inside the queue. */
  cond_signal (&queue->items_avail, &queue->lock);

  lock_release (&queue->lock);
}

