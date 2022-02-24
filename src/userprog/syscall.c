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

struct fd_to_file {
  int fd;
  struct file * file;
  bool active;
  int tid;
};

static struct fd_to_file fd_to_file[1024];

// DON'T REMOVE UNTIL DONE
// 1. static array of struct pointers [1024] for file descriptors
// 2. parents only wait for their children. list of running threads
// will be contained with the parent. design data structures so that
// a parent will only wait for their children. this can be added to
// thread.h. need to cover the case that the parent exits so that
// the children will exit.
// 3. use a pointer to a struct for the purpose of parent/child
// interactions.
// 4. exec () will call process_execute ().
// 5. use locks on the waiting structures.


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  fd_to_file[0].fd = 0;
  fd_to_file[0].file = NULL;
  fd_to_file[0].active = true;
  fd_to_file[0].tid = 0;

  fd_to_file[1].fd = 1;
  fd_to_file[1].file = NULL;
  fd_to_file[1].active = true;
  fd_to_file[1].tid = 0;
  
  fd_to_file[2].fd = 2;
  fd_to_file[2].file = NULL;
  fd_to_file[2].active = true;
  fd_to_file[2].tid = 0;
  
  for (int index = 3; index < 1024; index ++)
    {
      fd_to_file[index].fd = index;
      fd_to_file[index].file = NULL;
      fd_to_file[index].active = false;
      fd_to_file[index].tid = 0;
    }
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
          const char *cmd_line = (char *)us[1];
          validate_ptr ((void *)cmd_line);
          f->eax = (uint32_t)sys_exec (cmd_line);
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

// pagedir.h, pagedir_get_page() use to check proper allocation
// checking values (has to be in both process.c and syscall.c)
// check below C0000...
// check if it maps to physical memory (if bad kill the user (thread_exit))
//
// test value by dereferencing
//  if value succeeds, you're good to go
//  else, use exception handler
//
// also check file descriptors
// file descriptors can only be between 3 to 1023, if they are not within this
// it is an error.


/*  */
void
validate_ptr(const void *addr)
{
  if (addr >=  PHYS_BASE || addr < (void*) 0x8048000)
    {
      sys_exit (-1);
    }

  if (pagedir_get_page(thread_current ()->pagedir, addr) == NULL)
    {
      sys_exit (-1);
    }
}

/*  */
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

/*  */
static void
sys_halt(void)
{
  shutdown_power_off ();
}

/*  */
void
sys_exit(int status)
{
  set_status (status);
  thread_exit ();
}

/*  */
static uint32_t
sys_exec(const char *cmd_line)
{
  return process_execute (cmd_line);
}

/*  */
static int
sys_wait(uint32_t pid)
{
  return process_wait (pid);
}

/*  */
static bool
sys_create(const char *file, unsigned initial_size)
{
  return filesys_create (file, initial_size);
}

/*  */
static bool
sys_remove(const char *file)
{
  return filesys_remove (file);
}

/*  */
static int
sys_open(const char* filename)
{
  struct file *file = filesys_open (filename);
  if (file == NULL)
    {
      return -1;
    }
  return set_next_fd (file, filename);
}

/*  */
static int
sys_filesize(int fd)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    sys_exit (-1);
  return (int)file_length(file);
}

/*  */
static int
sys_read(int fd, void *buffer, unsigned size)
{
  if (fd == 0) /* reading from stdin */
    {
      char *buf = buffer;
      for (unsigned int index = 0; index < size; index ++)
        {
          buf[index] = input_getc ();
        }
    }
  else
    {
      struct file *file = get_file_from_fd (fd);
      if (file == NULL)
        sys_exit (-1);
      return (int)file_read (file, buffer, size);
    }
  return 0;
}

/*  */
static int
sys_write(int fd, const void *buffer, unsigned size)
{
  if (fd == 1) /* writing to stdout */
    {
      putbuf (buffer, size);
    }
  else
    {
      struct file *file = get_file_from_fd (fd);
      if (file == NULL)
        sys_exit (-1);
      return (int)file_write (file, buffer, size);
    }
  return 0;
}

/*  */
static void
sys_seek(int fd, unsigned position)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    sys_exit (-1);
  file_seek (file, position);
}

/*  */
static unsigned
sys_tell(int fd)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    sys_exit (-1);
  return (unsigned)file_tell (file);
}

/*  */
static void
sys_close(int fd)
{
  struct file *file = fd_to_file[fd].file;
  if (file == NULL)
    sys_exit (-1);
  /* Checks if the file descriptor it is closing belong to the
   * current process */
  struct thread *cur = thread_current ();
  if (cur->tid != fd_to_file[fd].tid)
    return;
  
  file_close (fd_to_file[fd].file);
  fd_to_file[fd].file = NULL;
  if (fd != 0 && fd != 1 && fd != 2)
    {
      fd_to_file[fd].active = false;
    }
}

/*  */
static struct file *
get_file_from_fd (int fd)
{
  return fd_to_file[fd].file;
}

static int
set_next_fd (struct file *file, const char *filename)
{
  for (int index = 3; index < 1024; index ++)
    {
      if (!fd_to_file[index].active)
        {
          fd_to_file[index].file = file;
          fd_to_file[index].active = true;
          struct thread *cur = thread_current ();
          fd_to_file[index].tid = cur->tid;

          /* Check if it is a running executable */
          if (strcmp (filename, cur->name) == 0)
            file_deny_write (file);
          return fd_to_file[index].fd;
        }
    }
  return -1;
}
