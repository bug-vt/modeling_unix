#include "userprog/syscall.h"
#include <stdio.h>
// #include <unistd.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <filesys/file.h>
#include <filesys/filesys.h>
#include <filesys/inode.h>
#include <devices/input.h>
#include <lib/kernel/stdio.h>

static void syscall_handler (struct intr_frame *);

/* Our Code */
static void sys_halt (void);
static void sys_exit (int status);
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

//static int get_fd_from_file (struct file *file);
static struct file *get_file_from_fd (int fd);
static int set_next_fd (struct file *file);

struct fd_to_file {
  int fd;
  struct file * file;
  bool active;
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

  fd_to_file[1].fd = 1;
  fd_to_file[1].file = NULL;
  fd_to_file[1].active = true;
  
  fd_to_file[2].fd = 2;
  fd_to_file[2].file = NULL;
  fd_to_file[2].active = true;
  
  for (int index = 3; index < 1024; index ++)
    {
      fd_to_file[index].fd = index;
      fd_to_file[index].file = NULL;
      fd_to_file[index].active = false;
    }
}

static void
syscall_handler (struct intr_frame *f) 
{
  // printf ("system call!\n");
  uint32_t * userstack = (uint32_t *)f->esp;
  int sys_call_number = userstack[0];

  switch (sys_call_number)
    {
      case SYS_HALT:
        {
          sys_halt ();
          break;
        }
      case SYS_EXIT:
        {
          int status = userstack[1];
          sys_exit (status);
          break;
        }
      case SYS_EXEC:
        {
          const char *cmd_line = (char *)userstack[1];
          check_user_args ((void *)cmd_line);
          f->eax = (uint32_t)sys_exec (cmd_line);
          break;
        }
      case SYS_WAIT:
        {
          uint32_t pid = (uint32_t)userstack[1];
          f->eax = (uint32_t)sys_wait (pid);
          break;
        }
      case SYS_CREATE:
        {
          const char *file = (char *)userstack[1];
          unsigned initial_size = (unsigned)userstack[2];
          check_user_args ((void *)file);
          f->eax = (uint32_t)sys_create (file, initial_size);
          break;
        }
      case SYS_REMOVE:
        {
          const char *file = (char *)userstack[1];
          check_user_args ((void *)file);
          f->eax = (uint32_t)sys_remove (file);
          break;
        }
      case SYS_OPEN:
        {
          const char *file = (char *)userstack[1];
          check_user_args ((void *)file);
          f->eax = (uint32_t)sys_open (file);
          break;
        }
      case SYS_FILESIZE:
        {
          int fd = userstack[1];
          f->eax = (uint32_t)sys_filesize (fd);
          break;
        }
      case SYS_READ:
        {
          int fd = userstack[1];
          void *buffer = (void *)userstack[2];
          unsigned size = (unsigned)userstack[3];
          check_user_args (buffer);
          f->eax = (uint32_t)sys_read (fd, buffer, size);
          break;
        }
      case SYS_WRITE:
        {
          int fd = userstack[1];
          const void *buffer = (void *)userstack[2];
          unsigned size = (unsigned)userstack[3];
          check_const_user_args (buffer);
          f->eax = (uint32_t)sys_write (fd, buffer, size);
          break;
        }
      case SYS_SEEK:
        {
          int fd = userstack[1];
          unsigned position = (unsigned)userstack[2];
          sys_seek (fd, position);
          break;
        }
      case SYS_TELL:
        {
          int fd = userstack[1];
          f->eax = (uint32_t)sys_tell (fd);
          break;
        }
      case SYS_CLOSE:
        {
          int fd = userstack[1];
          sys_close (fd);
          break;
        }
    }
  // thread_exit ();
}

/*  */
void
check_user_args(void *arg UNUSED)
{
  ;
}
void
check_const_user_args (const void *arg UNUSED)
{
  ;
}

/*  */
static void
sys_halt(void)
{
  ;
}

/*  */
static void
sys_exit(int status UNUSED)
{
  thread_exit ();
}

/*  */
static uint32_t
sys_exec(const char *cmd_line UNUSED)
{
  return 0;
}

/*  */
static int
sys_wait(uint32_t pid UNUSED)
{
  return 0;
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
  return set_next_fd (file);
}

/*  */
static int
sys_filesize(int fd)
{
  struct file *file = get_file_from_fd (fd);
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
      return (int)file_write (file, buffer, size);
    }
  return 0;
}

/*  */
static void
sys_seek(int fd UNUSED, unsigned position UNUSED)
{
  ;
}

/*  */
static unsigned
sys_tell(int fd UNUSED)
{
  return 0;
}

/*  */
static void
sys_close(int fd)
{
  file_close (fd_to_file[fd].file);
  fd_to_file[fd].file = NULL;
  if (fd != 0 && fd != 1 && fd != 2)
    {
      fd_to_file[fd].active = false;
    }
}

// /*  */
// static int
// get_fd_from_file (struct file *file)
// {
//   for (int index = 3; index < 1024; index ++)
//     {
//       if (fd_to_file[index].file == file)
//         {
//           return fd_to_file[index].fd;
//         }
//     }
//   return NULL;
// }

/*  */
static struct file *
get_file_from_fd (int fd)
{
  return fd_to_file[fd].file;
}

static int
set_next_fd (struct file *file)
{
  for (int index = 3; index < 1024; index ++)
    {
      if (!fd_to_file[index].active)
        {
          fd_to_file[index].file = file;
          fd_to_file[index].active = true;
          return fd_to_file[index].fd;
        }
    }
  return -1;
}