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
#include "filesys/directory.h"
#include "lib/user/syscall.h"
#include "filesys/pipe.h"
#include "vm/mem.h"
#include <user/errno.h>
#include "devices/timer.h"

#define WORD_SIZE 4
#define SYSCALL1 WORD_SIZE 
#define SYSCALL2 (WORD_SIZE * 2)
#define SYSCALL3 (WORD_SIZE * 3)

static void syscall_handler (struct intr_frame *);

/* Our Code */
static void sys_halt (void);
static int sys_exec (const char *cmd_line);
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
static bool sys_chdir (const char *dir);
static bool sys_mkdir (const char *dir);
static bool sys_readdir (int fd, char *name);
static bool sys_isdir (int fd);
static int sys_inumber (int fd);
static int sys_fork (struct intr_frame *f);
static int sys_dup2 (int old_fd, int new_fd);
static int sys_pipe (int *pipefd);
static void sys_exec2 (const char *cmd_line);
static uintptr_t sys_sbrk (intptr_t increment);
static int64_t sys_times (void);
static void sys_sleep (int64_t ticks);

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
  /* Store copy of the user-provided file name */
  cur->syscall_arg = palloc_get_page (PAL_ZERO); 
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
          if (sys_create (file_name, args[1]))
            f->eax = true;
          else
            f->eax = -cur->errno;
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
      case SYS_CHDIR:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          str_copy_from_user (file_name, (char *) args[0]);
          f->eax = sys_chdir (file_name);
          break;
        }
      case SYS_MKDIR:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          str_copy_from_user (file_name, (char *) args[0]);
          f->eax = sys_mkdir (file_name);
          break;
        }
      case SYS_READDIR:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL2);
          validate_buffer ((void *) args[1], READDIR_MAX_LEN + 1);
          f->eax = sys_readdir (args[0], (char *) args[1]);
          break;
        }
      case SYS_ISDIR:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          f->eax = sys_isdir (args[0]);
          break;
        }
      case SYS_INUMBER:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          f->eax = sys_inumber (args[0]);
          break;
        }
      case SYS_FORK:
        {
          f->eax = sys_fork (f);
          break;
        }
      case SYS_DUP2:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL2);
          f->eax = sys_dup2 (args[0], args[1]);
          break;
        }
      case SYS_PIPE:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          validate_buffer ((void *) args[0], sizeof (int) * 2);
          f->eax = sys_pipe ((int *) args[0]);
          break;
        }
      case SYS_EXEC2:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          str_copy_from_user (file_name, (char *) args[0]);
          sys_exec2 (file_name);
          break;
        }
      case SYS_SBRK:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          f->eax = sys_sbrk (args[0]);
          break;
        }
      case SYS_TIMES:
        {
          f->eax = sys_times ();
          break;
        }
      case SYS_SLEEP:
        {
          copy_from_user (&args, stack_arg_addr, SYSCALL1);
          sys_sleep (args[0]);
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
      /* Limit user provided file name + arguments to be less than
         or equal to one page (4096 bytes).*/
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
static int 
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
  return filesys_create (file, initial_size);
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
    return -ENOENT;

  int fd = set_next_fd (file);
  if (fd == -EMFILE)
    file_close (file);

  return fd;
}

/* System call for obtaining the filesize */
static int
sys_filesize(int fd)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    return -EINVF;

  int size = (int)file_length(file);
  return size;
}

/* System call for reading */
static int
sys_read(int fd, void *buffer, unsigned size)
{
  struct file *file = get_file_from_fd (fd);

  int read = file_read (file, buffer, size);
  
  return read;
}

/* System call for writing */
static int
sys_write(int fd, const void *buffer, unsigned size)
{
  struct file *file = get_file_from_fd (fd);

  int written = file_write (file, buffer, size);

  return written;
}

/* System call for seeking */
static void
sys_seek(int fd, unsigned position)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    sys_exit (-1);

  file_seek (file, position);
}

/* System call for telling */
static unsigned
sys_tell(int fd)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    return -EINVF;

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
    return;
 
  dir_close (file_get_directory (file));
  file_close (file);
  cur->fd_table[fd] = NULL;
}

/* Obtains a file from file descriptor */
static struct file *
get_file_from_fd (int fd)
{
  if (fd < 0 || fd >= FD_MAX)
    return NULL;

  return thread_current ()->fd_table[fd];
}

/* Finds the next available file descriptor mapping and
 * sets the values within it. Also checks if the file
 * can be written into. */
static int
set_next_fd (struct file *file)
{
  struct file **fd_table = thread_current ()->fd_table;

  for (int fd = 0; fd < FD_MAX; fd++)
    {
      if (!fd_table[fd])
        {
          fd_table[fd] = file;
          return fd;
        } 
    }
  return -EMFILE;
}

/* Changes the current working directory of the process to dir,
   which may be relative or absolute. Returns true if
   successful, false on failure. */
static bool 
sys_chdir (const char *dir)
{
  struct dir *chdir = dir_traverse_path (dir, true); 
  if (!chdir)
    return false;
 
  thread_current ()->current_dir = dir_reopen (chdir);
  
  return true;
}

/* Creates the directory name dir, which may be relative or absolute.
   Return true if successful, false on failure.
   Fails if dir already exists or if any directory name in dir,
   besides the last, does not already exist. */
static bool 
sys_mkdir (const char *dir)
{
  return filesys_dir_create (dir, 0);
}

/* Reads a directory entry from file descriptor fd, which must represent
   a directory. If successful, stores the null-terminated file name
   in name, which must have room for READDIR_MAX_LEN + 1 bytes,
   and return true. If no entries are left in the directory, 
   return false. */
static bool sys_readdir (int fd, char *name)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL || file_get_directory (file) == NULL)
    return false;
   
  return dir_readdir (file_get_directory (file), name);
}

/* Returns true if fd represents a directory, 
   false if it represents an ordinary file. */
static bool sys_isdir (int fd)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL || file_get_directory (file) == NULL)
    return false;

  return true;
}

/* Returns the inode number of the inode associated with fd,
   which may represent an ordinary file or a directory. */
static int sys_inumber (int fd)
{
  struct file *file = get_file_from_fd (fd);
  if (file == NULL)
    return -1;

  struct inode *inode = file_get_inode (file);
  return inode_get_inumber (inode);
}

/* Create a new process by duplicating the calling process. */
static int 
sys_fork (struct intr_frame *f)
{
  return process_fork (f);
}


/* Allocates a new file descriptor NEW_FD that refers to the same
   open file description as the OLD_FD. */
static int
sys_dup2 (int old_fd, int new_fd)
{
  struct file *file = get_file_from_fd (old_fd);
  if (file == NULL)
    return -1;
  
  if (old_fd == new_fd)
    return new_fd;

  /* Close previously opened file in new_fd. */
  struct file *prev_file = get_file_from_fd (new_fd);
  file_close (prev_file);

  /* Duplicate file to new_fd. */
  struct thread *cur = thread_current();
  cur->fd_table[new_fd] = file_dup (file);

  return new_fd;
}

/* Create a pipe, a unidirectional data channel that can be
   used for interprocess communication. The array pipefd is
   used to return two file descriptors referring to the ends of
   the pipe. pipefd[0] refers to the read end whereas 
   pipefd[1] refers to the write end of the pipe. */
static int
sys_pipe (int *pipefd)
{
  struct file *read_end = NULL;
  struct file *write_end = NULL; 
  /* The limit on memory that can be allocated has been reached. */
  if (!pipe_open (&read_end, &write_end))
    return -1;
  
  ASSERT (read_end != NULL);
  ASSERT (write_end != NULL);

  pipefd[0] = set_next_fd (read_end);
  pipefd[1] = set_next_fd (write_end);
  /* The per-process limit on the number of open fd has been reached. */
  if (pipefd[0] == -1 || pipefd[1] == -1)
    {
      file_close (read_end);
      file_close (write_end);
      return -1;
    }

  return 0;
}

/* Replace the current process image with a new process image. */
static void 
sys_exec2 (const char *cmd_line)
{
  process_start ((char *) cmd_line);
}

/* Change the location of the program break.
   Increasing the program break has the effect of allocating memory
   to the process. */
static uintptr_t 
sys_sbrk (intptr_t increment)
{
  return mem_sbrk (increment);
}

/* Returns the number of clock ticks that have elapsed since
   the system was booted. */
static int64_t
sys_times (void)
{
  return timer_ticks ();
}

/* Sleeps for approximately TICKS clock ticks. */
static void
sys_sleep (int64_t ticks)
{
  timer_sleep (ticks);
}
