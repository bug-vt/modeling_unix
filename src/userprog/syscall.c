#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <filesys/file.h>
#include <filesys/filesys.h>

static void syscall_handler (struct intr_frame *);

/* Our Code */
static void check_user_args(void *arg);

static void sys_halt(void);
static void sys_exit(int status);
static pid_t sys_exec(const char *cmd_line);
static int sys_wait(pid_t pid);
static bool sys_create(const char *file, unsigned initial_size);
static bool sys_remove(const char *file);
static int sys_open(const char* file);
static int sys_filesize(int fd);
static int sys_read(int fd, void *buffer, unsigned size);
static int sys_write(int fd, const void *buffer, unsigned size);
static void sys_seek(int fd, unsigned position);
static unsigned sys_tell(int fd);
static void sys_close(int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  // printf ("system call!\n");
  int sys_call_number = *((uint8_t*)(f->esp + 1));

  switch (sys_call_number)
    {
      case SYS_HALT:
        {
          break;
        }
      case SYS_EXIT:
        {
          break;
        }
      case SYS_EXEC:
        {
          break;
        }
      case SYS_WAIT:
        {
          break;
        }
      case SYS_CREATE:
        {
          break;
        }
      case SYS_REMOVE:
        {
          break;
        }
      case SYS_OPEN:
        {
          break;
        }
      case SYS_FILESIZE:
        {
          break;
        }
      case SYS_READ:
        {
          break;
        }
      case SYS_WRITE:
        {
          break;
        }
      case SYS_SEEK:
        {
          break;
        }
      case SYS_TELL:
        {
          break;
        }
      case SYS_CLOSE:
        {
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