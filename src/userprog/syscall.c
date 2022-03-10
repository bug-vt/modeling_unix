#include "userprog/syscall.h"
#include <stdio.h>
// #include <unistd.h>
#include <syscall-nr.h>
#include <threads/interrupt.h>
#include <threads/thread.h>
#include <filesys/file.h>
#include <filesys/filesys.h>
#include <filesys/inode.h>
#include <devices/input.h>
#include <lib/kernel/stdio.h>
#include <userprog/process.h>
#include <devices/shutdown.h>

#include <threads/vaddr.h>
#include <userprog/pagedir.h>
#include <lib/string.h>
#include <threads/synch.h>
#include "threads/palloc.h"

static void syscall_handler (struct intr_frame *);

/* Our Code */
static void sys_halt (void);
static uint32_t sys_exec (const char *cmd_line);
static int sys_wait (uint32_t pid);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char* file);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_write (int fd, const void *buffer, unsigned size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);

static struct file *get_file_from_fd (int fd);
static int set_next_fd (struct file *file, const char *filename);
static void validate_fd (int fd, int syscall);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&filesys_lock);
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t * us = (uint32_t *)f->esp;
  validate_ptr ((void*)us);
  int sys_call_number = us[0];

  switch (sys_call_number)
    {
      case SYS_HALT:
        {
          sys_halt ();
          break;
        }
      case SYS_EXIT:
        {
          int status = us[1];
          status = status <= -1 ? -1 : status;
          f->eax = status;
          sys_exit (status);
          break;
        }
      case SYS_EXEC:
        {
          validate_ptr ((void *) us[1]);
          /* Temporary fix to allocate and copy cmd_line to kernel heap. */
          char *cmd_line = palloc_get_page (0);   
          if (cmd_line == NULL)
            {
              f->eax = TID_ERROR;
              break;
            }
          strlcpy (cmd_line, (char *) us[1], PGSIZE);   
          f->eax = (uint32_t) sys_exec (cmd_line);
          palloc_free_page (cmd_line);
          break;
        }
      case SYS_WAIT:
        {
          uint32_t pid = (uint32_t)us[1];
          f->eax = (uint32_t)sys_wait (pid);
          break;
        }
      case SYS_CREATE:
        {
          const char *file = (char *)us[1];
          unsigned initial_size = (unsigned)us[2];
          validate_ptr ((void *)file);
          f->eax = (uint32_t)sys_create (file, initial_size);
          break;
        }
      case SYS_REMOVE:
        {
          const char *file = (char *)us[1];
          validate_ptr ((void *)file);
          f->eax = (uint32_t)sys_remove (file);
          break;
        }
      case SYS_OPEN:
        {
          const char *file = (char *)us[1];
          validate_ptr ((void *)file);
          f->eax = (uint32_t)sys_open (file);
          break;
        }
      case SYS_FILESIZE:
        {
          validate_ptr ((void *) us + 1);
          int fd = us[1];
          validate_fd (fd, sys_call_number);
          f->eax = (uint32_t)sys_filesize (fd);
          break;
        }
      case SYS_READ:
        {
          int fd = us[1];
          validate_fd (fd, sys_call_number);
          void *buffer = (void *)us[2];
          validate_ptr (buffer);
          unsigned size = (unsigned)us[3];
          f->eax = (uint32_t)sys_read (fd, buffer, size);
          break;
        }
      case SYS_WRITE:
        {
          int fd = us[1];
          validate_fd (fd, sys_call_number);
          const void *buffer = (void *)us[2];
          validate_ptr (buffer);
          unsigned size = (unsigned)us[3];
          f->eax = (uint32_t)sys_write (fd, buffer, size);
          break;
        }
      case SYS_SEEK:
        {
          int fd = us[1];
          validate_fd (fd, sys_call_number);
          unsigned position = (unsigned)us[2];
          sys_seek (fd, position);
          break;
        }
      case SYS_TELL:
        {
          validate_ptr ((void*) us + 1);
          int fd = us[1];
          validate_fd (fd, sys_call_number);
          f->eax = (uint32_t)sys_tell (fd);
          break;
        }
      case SYS_CLOSE:
        {
          validate_ptr ((void*) us + 1);
          int fd = us[1];
          validate_fd (fd, sys_call_number);
          sys_close (fd);
          break;
        }
    }
}

/* Checks if the current pointer is a valid one */
void
validate_ptr(const void *addr)
{
  if (!is_user_vaddr (addr))
    {
      sys_exit (-1);
    }

  if (pagedir_get_page(thread_current ()->pagedir, addr) == NULL)
    {
      sys_exit (-1);
    }
}

/* Checks if the current file descriptor is a valid one */
static void
validate_fd (int fd, int syscall)
{
  if (fd == 0 && syscall == SYS_READ)
    {
      ; /* Do nothing */
    }
  else if (fd == 1 && syscall == SYS_WRITE)
    {
      ; /* Do nothing */
    }
  else if (fd < 3 || fd > 1023)
    {
      sys_exit (-1);
    }
}

/* System call for halting */
static void
sys_halt(void)
{
  shutdown_power_off ();
}

/* System call for exiting */
void
sys_exit(int status)
{
  struct thread *current = thread_current ();
  current->bond->status = status;
  thread_exit ();
}

/* System call for executing */
static uint32_t
sys_exec(const char *cmd_line)
{
  return process_execute (cmd_line);
}

/* System call for waiting */
static int
sys_wait(uint32_t pid)
{
  return process_wait (pid);
}

/* System call for creating */
static bool
sys_create(const char *file, unsigned initial_size)
{
  lock_acquire (&filesys_lock);
  bool val = filesys_create (file, initial_size);
  lock_release (&filesys_lock);
  return val;
}

/* System call for removing */
static bool
sys_remove(const char *file)
{
  lock_acquire (&filesys_lock);
  bool val = filesys_remove (file);
  lock_release (&filesys_lock);
  return val;
}

/* System call for opening */
static int
sys_open(const char* filename)
{
  lock_acquire (&filesys_lock);
  struct file *file = filesys_open (filename);
  if (file == NULL)
    {
      lock_release (&filesys_lock);
      return -1;
    }
  int fd = set_next_fd (file, filename);
  lock_release (&filesys_lock);
  return fd;
}

/* System call for obtaining the filesize */
static int
sys_filesize(int fd)
{
  lock_acquire (&filesys_lock);
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    {
      lock_release (&filesys_lock);
      sys_exit (-1);
    }
  int size = (int)file_length(file);
  lock_release (&filesys_lock);
  return size;
}

/* System call for reading */
static int
sys_read(int fd, void *buffer, unsigned size)
{
  if (fd == 0) /* reading from stdin */
    {
      lock_acquire (&filesys_lock);
      char *buf = buffer;
      for (unsigned int index = 0; index < size; index ++)
        {
          buf[index] = input_getc ();
        }
      lock_release (&filesys_lock);
    }
  else
    {
      lock_acquire (&filesys_lock);
      struct file *file = get_file_from_fd (fd);
      if (file == NULL)
        {
          lock_release (&filesys_lock);
          sys_exit (-1);
        }
      int fsize = file_read (file, buffer, size);
      lock_release (&filesys_lock);
      return fsize;
    }
  return 0;
}

/* System call for writing */
static int
sys_write(int fd, const void *buffer, unsigned size)
{
  if (fd == 1) /* writing to stdout */
    {
      lock_acquire (&filesys_lock);
      putbuf (buffer, size);
      lock_release (&filesys_lock);
    }
  else
    {
      lock_acquire (&filesys_lock);
      struct file *file = get_file_from_fd (fd);
      if (file == NULL)
        {
          lock_release (&filesys_lock);
          sys_exit (-1);
        }
      int fsize = file_write (file, buffer, size);
      lock_release (&filesys_lock);
      return fsize;
    }
  return 0;
}

/* System call for seeking */
static void
sys_seek(int fd, unsigned position)
{
  lock_acquire (&filesys_lock);
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    {
      lock_release (&filesys_lock);
      sys_exit (-1);
    }
  file_seek (file, position);
  lock_release (&filesys_lock);
}

/* System call for telling */
static unsigned
sys_tell(int fd)
{
  lock_acquire (&filesys_lock);
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    {
      lock_release (&filesys_lock);
      sys_exit (-1);
    }
  int position = (unsigned)file_tell (file);
  lock_release (&filesys_lock);
  return position;
}

/* System call for closing */
static void
sys_close(int fd)
{
  struct thread *cur = thread_current ();
  lock_acquire (&filesys_lock);
  struct file *file = cur->fd_table->fd_to_file[fd];
  if (file == NULL)
    {
      lock_release (&filesys_lock);
      sys_exit (-1);
    }
  
  file_close (cur->fd_table->fd_to_file[fd]);
  cur->fd_table->fd_to_file[fd] = NULL;
  lock_release (&filesys_lock);
}

/* Obtains a file from file descriptor */
static struct file *
get_file_from_fd (int fd)
{
  struct thread *cur = thread_current ();
  return cur->fd_table->fd_to_file[fd];
}

/* Finds the next available file descriptor mapping and
 * sets the values within it. Also checks if the file
 * can be written into. */
static int
set_next_fd (struct file *file, const char *filename)
{
  struct thread *cur = thread_current ();
  for (int fd = 3; fd < 1024; fd ++)
    {
      if (cur->fd_table->fd_to_file[fd] == NULL)
        {
          cur->fd_table->fd_to_file[fd] = file;

          /* Check if it is a running executable */
          if (strcmp (filename, cur->name) == 0)
            file_deny_write (file);
          return fd;
        }
    }
  return -1;
}
