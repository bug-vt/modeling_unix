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

#define WORD_SIZE 4
#define SYSCALL1 WORD_SIZE 
#define SYSCALL2 (WORD_SIZE * 2)
#define SYSCALL3 (WORD_SIZE * 3)

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
static int set_next_fd (struct file *file);
static void validate_ptr (const void *addr);
static void validate_buffer (void *buffer, unsigned size);
static int get_user (const uint8_t *uaddr);
static void copy_from_user (void *to, const void *user, size_t bytes);
static void str_copy_from_user (char *dst, const char *user);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  int syscall_number;
  copy_from_user (&syscall_number, f->esp, WORD_SIZE);
  
  int args[3];  /* Array holding maximum of 3 arguments. */
  void *stack_arg_addr = f->esp + WORD_SIZE; /* Starting address of the arguments
                                                in stack */
  struct thread *cur = thread_current ();
  cur->syscall_arg = palloc_get_page (PAL_ZERO); /* Will hold copy of the user-provied file name */
  char *file_name = cur->syscall_arg;
  if (cur->syscall_arg == NULL)
    sys_exit (-1);

  switch (syscall_number)
    {
      case SYS_HALT:
        {
          sys_halt ();
          break;
        }
      case SYS_EXIT:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          f->eax = args[0];
          sys_exit (args[0]);
          break;
        }
      case SYS_EXEC:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          str_copy_from_user (file_name, (char *) args[0]);
          f->eax = sys_exec (file_name);
          break;
        }
      case SYS_WAIT:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          f->eax = sys_wait (args[0]);
          break;
        }
      case SYS_CREATE:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL2);
          str_copy_from_user (file_name, (char *) args[0]);
          f->eax = sys_create (file_name, args[1]);
          break;
        }
      case SYS_REMOVE:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          str_copy_from_user (file_name, (char *) args[0]);
          f->eax = sys_remove (file_name);
          break;
        }
      case SYS_OPEN:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          str_copy_from_user (file_name, (char *) args[0]);
          f->eax = sys_open (file_name);
          break;
        }
      case SYS_FILESIZE:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          f->eax = sys_filesize (args[0]);
          break;
        }
      case SYS_READ:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL3);
          validate_buffer ((void *) args[1], args[2]);
          f->eax = sys_read (args[0], (char *) args[1], args[2]);
          break;
        }
      case SYS_WRITE:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL3);
          validate_buffer ((void *) args[1], args[2]);
          f->eax = sys_write (args[0], (char *) args[1], args[2]);
          break;
        }
      case SYS_SEEK:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL2);
          sys_seek (args[0], args[1]);
          break;
        }
      case SYS_TELL:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          f->eax = sys_tell (args[0]);
          break;
        }
      case SYS_CLOSE:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          sys_close (args[0]);
          break;
        }
    }

  palloc_free_page (cur->syscall_arg);
  cur->syscall_arg = NULL;
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

/* Copies the user data into the kernel space. */
static void
copy_from_user (void *to, const void *user, size_t bytes)
{
  char *copy = (char *) to;  
  for (size_t index = 0; index < bytes; index ++)
    {
      if (!is_user_vaddr (user + index))
        sys_exit (-1);

      int copied_byte = get_user ((uint8_t *)(user + index));
      if (copied_byte == -1)
        sys_exit (-1);

      copy[index] = copied_byte;
    }
}

/* Copies the string user data to the kernel. */
static void
str_copy_from_user (char *dst, const char *user)
{
  char *copy = dst;
  int index = 0;
  do
    {
      /* Prevent buffer overflow attacks. */
      if (index >= PGSIZE)
        sys_exit (-1);

      if (!is_user_vaddr (user + index))
        sys_exit (-1);

      int copied_byte = get_user ((uint8_t *)(user + index));
      if (copied_byte == -1)
        sys_exit (-1);

      copy[index] = copied_byte;

    } while (*(user + index ++) != '\0');

  copy[index] = '\0';
}

/* Checks if the given buffer span over the valid address range. */
static void
validate_buffer (void *buffer, unsigned size)
{
  if (size == 0)
    return;
  /* Checks if the size of the buffer is too big */
  if (buffer > buffer + size)
    sys_exit (-1);
  /* For loop checks if pages allocated for the buffer
     is within the valid user address range. */
  for (const void *addr = buffer; addr < buffer + size; addr += PGSIZE)
    validate_ptr (addr);

  /* Checks the last byte of the buffer,
     since it could located in the middle of the page. */
  validate_ptr (buffer + size - 1);
}

/* Checks if the current pointer is a valid one */
void
validate_ptr (const void *addr)
{
  if (!is_user_vaddr (addr))
    sys_exit (-1);

  if (get_user ((uint8_t *) addr) == -1)
    sys_exit (-1);
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
  bool val = filesys_create (file, initial_size);
  return val;
}

/* System call for removing */
static bool
sys_remove(const char *file)
{
  bool val = filesys_remove (file);
  return val;
}

/* System call for opening */
static int
sys_open(const char* filename)
{
  struct file *file = filesys_open (filename);
  if (file == NULL)
    {
      return -1;
    }
  int fd = set_next_fd (file);
  if (fd == -1)
    {
      file_close (file);
    }
  return fd;
}

/* System call for obtaining the filesize */
static int
sys_filesize(int fd)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    {
      return -1;
    }
  int size = (int)file_length(file);
  return size;
}

/* System call for reading */
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
      return size;
    }

  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    return -1;

  int read = file_read (file, buffer, size);
  
  return read;
}

/* System call for writing */
static int
sys_write(int fd, const void *buffer, unsigned size)
{
  if (fd == 1) /* writing to stdout */
    {
      putbuf (buffer, size);
      return size;
    }

  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    return -1;

  int written = file_write (file, buffer, size);

  return written;
}

/* System call for seeking */
static void
sys_seek(int fd, unsigned position)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    {
      sys_exit (-1);
    }
  file_seek (file, position);
}

/* System call for telling */
static unsigned
sys_tell(int fd)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    {
      return -1;
    }
  int position = (unsigned)file_tell (file);
  return position;
}

/* System call for closing */
static void
sys_close(int fd)
{
  struct thread *cur = thread_current ();
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    {
      sys_exit (-1);
    }
  
  file_close (cur->fd_table->fd_to_file[fd]);
  cur->fd_table->fd_to_file[fd] = NULL;
}

/* Obtains a file from file descriptor */
static struct file *
get_file_from_fd (int fd)
{
  if (fd < 0 || fd >= 1024)
    return NULL;

  return thread_current ()->fd_table->fd_to_file[fd];
}

/* Finds the next available file descriptor mapping and
 * sets the values within it. Also checks if the file
 * can be written into. */
static int
set_next_fd (struct file *file)
{
  struct file **fd_table = thread_current ()->fd_table->fd_to_file;
  for (int fd = 2; fd < 1024; fd++)
    {
      if (!fd_table[fd])
        {
          fd_table[fd] = file;
          return fd;
        } 
    }
  return -1;
}
