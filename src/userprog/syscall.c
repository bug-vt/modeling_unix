#include "userprog/syscall.h"
#include <stdio.h>
// #include <unistd.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <filesys/file.h>
#include <filesys/filesys.h>

static void syscall_handler (struct intr_frame *);

/* Our Code */
static void check_user_args (void *arg);

static void sys_halt (void);
static void sys_exit (int status);
static uin32_t sys_exec (const char *cmd_line);
static int sys_wait (uin32_t pid);
static bool sys_create (const char *file, unsigned initial_size);
static bool sys_remove (const char *file);
static int sys_open (const char* file);
static int sys_filesize (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_write (int fd, const void *buffer, unsigned size);
static void sys_seek (int fd, unsigned position);
static unsigned sys_tell (int fd);
static void sys_close (int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
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
          uin32_t pid = (uin32_t)userstack[1];
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
          check_user_args (buffer);
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
  thread_exit ();
}

/*  */
static void
check_user_args(void *arg)
{
  return 0;
}

/*  */
static void
sys_halt(void)
{
  return 0;
}

/*  */
static void
sys_exit(int status)
{
  return 0;
}

/*  */
static pid_t
sys_exec(const char *cmd_line)
{
  return 0;
}

/*  */
static int
sys_wait(pid_t pid)
{
  return 0;
}

/*  */
static bool
sys_create(const char *file, unsigned initial_size)
{
  return 0;
}

/*  */
static bool
sys_remove(const char *file)
{
  return 0;
}

/*  */
static int
sys_open(const char* file)
{
  return 0;
}

/*  */
static int
sys_filesize(int fd)
{
  return 0;
}

/*  */
static int
sys_read(int fd, void *buffer, unsigned size)
{
  return 0;
}

/*  */
static int
sys_write(int fd, const void *buffer, unsigned size)
{
  return 0;
}

/*  */
static void
sys_seek(int fd, unsigned position)
{
  return 0;
}

/*  */
static unsigned
sys_tell(int fd)
{
  return 0;
}

/*  */
static void
sys_close(int fd)
{
  return 0;
}