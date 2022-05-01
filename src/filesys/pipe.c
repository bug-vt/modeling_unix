#include "filesys/pipe.h"
#include "threads/thread.h"
#include <stdio.h>
#include "threads/malloc.h"

static bool pipe_empty (struct pipe *);
static bool pipe_full (struct pipe *);
static uint8_t pipe_dequeue (struct pipe *);
static void pipe_enqueue (struct pipe *, uint8_t);

/* Create new pipe. */
struct pipe *
pipe_create (size_t capacity)
{
  struct pipe *pipe = malloc (sizeof (struct pipe));
  if (pipe == NULL)
    return NULL;

  pipe->buffer = malloc ((capacity + 1) * sizeof (uint8_t));
  if (pipe->buffer == NULL)
    {
      free (pipe);
      return NULL;
    }
  /* Since one spot in the pipe must be empty, 
     add one more spot from the true capacity. */
  pipe->capacity = capacity + 1;
  pipe->head = 0;
  pipe->tail = 0;
  pipe->read_end = NULL;
  pipe->write_end = NULL;
  lock_init (&pipe->lock);
  cond_init (&pipe->items_avail);
  cond_init (&pipe->slots_avail);

  return pipe;
}

/* Return true if the pipe is empty. False otherwise. */
bool 
pipe_empty (struct pipe *pipe)
{
  ASSERT (pipe != NULL);
  return pipe->head == pipe->tail;
}

/* Return true if the pipe is full. False otherwise. */
bool 
pipe_full (struct pipe *pipe)
{
  ASSERT (pipe != NULL);
  return pipe->head == (pipe->tail + 1) % pipe->capacity;
}

/* Pop the element from the front of the pipe. */
uint8_t
pipe_dequeue (struct pipe *pipe)
{
  ASSERT (pipe != NULL);
  uint8_t item = 0; 
  lock_acquire (&pipe->lock);
  /* Wait if pipe is empty. */
  while (pipe_empty (pipe))
    {
      if (pipe->write_end == NULL)
        goto dequeue_done;
      cond_wait (&pipe->items_avail, &pipe->lock);
    }

  /* Move the head to point to front element. */
  pipe->head = (pipe->head + 1) % pipe->capacity;
  item = pipe->buffer[pipe->head];
  /* Notify that there is at least one free spot inside the pipe. */
  cond_signal (&pipe->slots_avail, &pipe->lock);

dequeue_done:
  lock_release (&pipe->lock);
  return item;
}

/* Push the given element to the back of the pipe. */
void
pipe_enqueue (struct pipe *pipe, uint8_t item)
{
  ASSERT (pipe != NULL);
  lock_acquire (&pipe->lock);
  /* Wait if pipe is full. */
  while (pipe_full (pipe))
    {
      if (pipe->read_end == NULL)
        goto enqueue_done;
      cond_wait (&pipe->slots_avail, &pipe->lock);
    }

  /* Move the tail to point to empty spot follow by the last element. */
  pipe->tail = (pipe->tail + 1) % pipe->capacity;
  pipe->buffer[pipe->tail] = item;
  /* Notify that there is at least one element inside the pipe. */
  cond_signal (&pipe->items_avail, &pipe->lock);

enqueue_done:
  lock_release (&pipe->lock);
}

/* Allocate two files as read end and write end of the pipe. */
bool
pipe_open (struct file **read_end, struct file **write_end)
{
  struct pipe *pipe = pipe_create (512);
  if (pipe == NULL)
    return false;

  if (!file_pipe_ends (pipe, read_end, write_end))
    {
      free (pipe->buffer);
      free (pipe);
      return false;
    }

  pipe->read_end = *read_end;
  pipe->write_end = *write_end;

  return true;
}

/* Closes pipe for given read/write end. */
void
pipe_close (struct pipe *pipe, struct file *file)
{
  if (pipe)
    {
      if (file == pipe->read_end)
        {
          pipe->read_end = NULL;
          lock_acquire (&pipe->lock);
          cond_signal (&pipe->slots_avail, &pipe->lock);
          lock_release (&pipe->lock);
        }
      else if (file == pipe->write_end)
        {
          pipe->write_end = NULL;
          lock_acquire (&pipe->lock);
          cond_signal (&pipe->items_avail, &pipe->lock);
          lock_release (&pipe->lock);
        }

      /* Release resources for pipe if both read end and write end are
         closed. */
      if (pipe->read_end == NULL && pipe->write_end == NULL)
        {
          free (pipe->buffer);
          free (pipe);
        }
    }
}

/* Read SIZE bytes from the pipe. */
int
pipe_read (struct pipe *pipe, void *buffer_, off_t size)
{
  ASSERT (pipe != NULL);
  ASSERT (buffer_ != NULL);

  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
   
  for (bytes_read = 0; bytes_read < size; bytes_read++)
  {
    buffer[bytes_read] = pipe_dequeue (pipe);
    if (buffer[bytes_read] == 0)
      break;
  }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into pipe.
   Returns the number of bytes actually written,
   which may be less than SIZE if end of file is reached. */
int 
pipe_write (struct pipe *pipe, const void *buffer_, off_t size)
{
  ASSERT (pipe != NULL);
  ASSERT (buffer_ != NULL);

  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
   
  for (bytes_written = 0; bytes_written < size; bytes_written++)
    {
      pipe_enqueue (pipe, buffer[bytes_written]);
      if (buffer[bytes_written] == 0)
        break;
    }

  return bytes_written;
}

/* Return read end of the pipe. */
struct file *
pipe_read_end (struct pipe *pipe)
{
  return pipe->read_end;
}

/* Return write end of the pipe. */
struct file *
pipe_write_end (struct pipe *pipe)
{
  return pipe->write_end;
}
