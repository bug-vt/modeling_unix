#include "filesys/file.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "string.h"
#include <list.h>
#include "threads/synch.h"
#include "lib/kernel/queue.h"
#include "stdio.h"
#include "devices/input.h"
#include <user/errno.h>


static struct list open_files;
static struct lock open_files_lock;


static int read_error (struct file *file);
static int write_error (struct file *file);

/* An open file. */
struct file 
  {
    struct list_elem elem;      /* Element in open file list. */
    file_type type;             /* Type of file. */
    struct inode *inode;        /* File's inode. */
    off_t pos;                  /* Current position. */
    bool deny_write;            /* Has file_deny_write() been called? */
    int ref_count;              /* Number of referencing fd. */
    struct dir *dir;            /* Used when file is directory. */
    struct pipe *pipe;          /* Used when file is Pipe end. */
  };

/* Initialize open file list, keeping track of files that provide 
   layer of indirection between file descriptor and inode. */
void
file_init (void)
{
  list_init (&open_files);
  lock_init (&open_files_lock);
}

/* Open console, STDIN or STDOUT, and returns as a file. */
struct file *
file_open_console (file_type type)
{
  if (type != STDIN && type != STDOUT)
    return NULL;

  struct file *file = calloc (1, sizeof *file);
  file->type = type;
  file->ref_count = 1;
  
  lock_acquire (&open_files_lock);
  list_push_front (&open_files, &file->elem);
  lock_release (&open_files_lock);

  return file;
}

/* Opens a file for the given INODE, of which it takes ownership,
   and returns the new file.  Returns a null pointer if an
   allocation fails or if INODE is null. */
struct file *
file_open (struct inode *inode) 
{
  struct file *file = calloc (1, sizeof *file);
  if (inode != NULL && file != NULL)
    {
      if (inode_is_dir (inode))
        {
          file->type = DIR;
          file->dir = dir_open (inode);
        }
      else
        {
          file->type = REG;
          file->dir = NULL;
        }
      file->inode = inode;
      file->pos = 0;
      file->deny_write = false;
      file->ref_count = 1;
      file->pipe = NULL;

      lock_acquire (&open_files_lock);
      list_push_front (&open_files, &file->elem);
      lock_release (&open_files_lock);

      return file;
    }
  else
    {
      inode_close (inode);
      free (file);
      return NULL; 
    }
}

/* Opens and returns a new file for the same inode as FILE.
   Returns a null pointer if unsuccessful. */
struct file *
file_reopen (struct file *file) 
{
  return file_open (inode_reopen (file->inode));
}

/* Closes FILE. */
void
file_close (struct file *file) 
{
  if (file != NULL)
    {
      /* Release resources if this was the last reference. */
      if (--file->ref_count == 0)
        {
          file_allow_write (file);
          inode_close (file->inode);
          lock_acquire (&open_files_lock);
          list_remove (&file->elem);
          lock_release (&open_files_lock);

          pipe_close (file->pipe, file);

          free (file);
        }
    }
}

/* Returns the inode encapsulated by FILE. */
struct inode *
file_get_inode (struct file *file) 
{
  return file->inode;
}

/* Duplicate file by incrementing reference count. */
struct file *
file_dup (struct file *file)
{
  if (file != NULL)
    {
      ASSERT (file->ref_count > 0);
      lock_acquire (&open_files_lock);
      file->ref_count++;
      lock_release (&open_files_lock);
    }

  return file;
}

/* If given file is invalid for read, return corresponding error number 
   specified in errno.h */
static int
read_error (struct file *file)
{
  if (!file)
    return EINVF;
  if (file->type == PIPE && file != pipe_read_end (file->pipe))
    return EBADF;
  if (file->type == DIR)
    return EISDIR;

  return 0;
}

/* Reads SIZE bytes from FILE into BUFFER,
   starting at the file's current position.
   Returns the number of bytes actually read,
   which may be less than SIZE if end of file is reached.
   Advances FILE's position by the number of bytes read. */
off_t
file_read (struct file *file, void *buffer, off_t size) 
{
  int error = read_error (file);
  if (error)
    return -error;

  off_t bytes_read = -1;
  if (file->type == STDIN)
    {
      char *buf = buffer;
      for (bytes_read = 0; bytes_read < size; bytes_read++)
        buf[bytes_read] = input_getc ();
    }
  else if (file->type == STDOUT)
    return 0;
  else if (file->type == PIPE)
    bytes_read = pipe_read (file->pipe, buffer, size);
  else if (file->type == REG)
    {
      bytes_read = inode_read_at (file->inode, buffer, size, file->pos);
      file->pos += bytes_read;
    }
  return bytes_read;
}

/* Reads SIZE bytes from FILE into BUFFER,
   starting at offset FILE_OFS in the file.
   Returns the number of bytes actually read,
   which may be less than SIZE if end of file is reached.
   The file's current position is unaffected. */
off_t
file_read_at (struct file *file, void *buffer, off_t size, off_t file_ofs) 
{
  return inode_read_at (file->inode, buffer, size, file_ofs);
}

/* If given file is invalid for write, return corresponding error number 
   specified in errno.h */
static int
write_error (struct file *file)
{
  if (!file)
    return EINVF;
  if (file->type == PIPE && file != pipe_write_end (file->pipe))
    return EBADF;
  if (file->type == DIR)
    return EISDIR;

  return 0;
}

/* Writes SIZE bytes from BUFFER into FILE,
   starting at the file's current position.
   Returns the number of bytes actually written,
   which may be less than SIZE if end of file is reached.
   (Normally we'd grow the file in that case, but file growth is
   not yet implemented.)
   Advances FILE's position by the number of bytes read. */
off_t
file_write (struct file *file, const void *buffer, off_t size) 
{
  int error = write_error (file);
  if (error)
    return -error;

  off_t bytes_written = -1;
  if (file->type == STDIN)
    return 0;
  else if (file->type == STDOUT)
    {
      putbuf (buffer, size);
      bytes_written = size;
    }
  else if (file->type == PIPE)
    bytes_written = pipe_write (file->pipe, buffer, size);
  else if (file->type == REG)
    {
      bytes_written = inode_write_at (file->inode, buffer, size, file->pos);
      file->pos += bytes_written;
    }
  return bytes_written;
}

/* Writes SIZE bytes from BUFFER into FILE,
   starting at offset FILE_OFS in the file.
   Returns the number of bytes actually written,
   which may be less than SIZE if end of file is reached.
   (Normally we'd grow the file in that case, but file growth is
   not yet implemented.)
   The file's current position is unaffected. */
off_t
file_write_at (struct file *file, const void *buffer, off_t size,
               off_t file_ofs) 
{
  return inode_write_at (file->inode, buffer, size, file_ofs);
}

/* Prevents write operations on FILE's underlying inode
   until file_allow_write() is called or FILE is closed. */
void
file_deny_write (struct file *file) 
{
  ASSERT (file != NULL);
  if (!file->deny_write) 
    {
      file->deny_write = true;
      inode_deny_write (file->inode);
    }
}

/* Re-enables write operations on FILE's underlying inode.
   (Writes might still be denied by some other file that has the
   same inode open.) */
void
file_allow_write (struct file *file) 
{
  ASSERT (file != NULL);
  if (file->deny_write) 
    {
      file->deny_write = false;
      inode_allow_write (file->inode);
    }
}

/* Returns the size of FILE in bytes. */
off_t
file_length (struct file *file) 
{
  ASSERT (file != NULL);
  return inode_length (file->inode);
}

/* Sets the current position in FILE to NEW_POS bytes from the
   start of the file. */
void
file_seek (struct file *file, off_t new_pos)
{
  ASSERT (file != NULL);
  ASSERT (new_pos >= 0);
  file->pos = new_pos;
}

/* Returns the current position in FILE as a byte offset from the
   start of the file. */
off_t
file_tell (struct file *file) 
{
  ASSERT (file != NULL);
  return file->pos;
}

/* Extract file name from path. */
char *
file_name_from_path (const char *path)
{
  char *file_name;
  /* Extract file name from the path */
  if (strchr (path, '/'))
    file_name = strrchr (path, '/') + 1;
  else
    file_name = (char *) path;

  return file_name;
}

/* Return corresponding directory if the given file is a directory */
struct dir *
file_get_directory (struct file *file)
{
  return file->dir;
}

/* Allocate two files as read end and write end of the pipe. */
bool
file_pipe_ends (struct pipe *pipe, struct file **read_end, struct file **write_end)
{
  if ((*read_end = calloc (1, sizeof (struct file))) == NULL)
    goto pipe_ends_err;
  if ((*write_end = calloc (1, sizeof (struct file))) == NULL)
    goto pipe_ends_err;

  /* Initialize read end of the pipe. */
  (*read_end)->type = PIPE;
  (*read_end)->ref_count = 1;
  (*read_end)->pipe = pipe;
  lock_acquire (&open_files_lock);
  list_push_front (&open_files, &(*read_end)->elem);
  lock_release (&open_files_lock);

  /* Initialize wirte end of the pipe. */
  (*write_end)->type = PIPE;
  (*write_end)->ref_count = 1;
  (*write_end)->pipe = pipe;
  lock_acquire (&open_files_lock);
  list_push_front (&open_files, &(*write_end)->elem);
  lock_release (&open_files_lock);

  return true;

pipe_ends_err:
  if (*read_end)
    free (*read_end);
  if (*write_end)
    free (*write_end);
  return false;
}

