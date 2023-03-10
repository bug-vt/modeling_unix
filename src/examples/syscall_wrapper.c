/**
 * syscall_wrapper.c
 * Author : Bug Lee (bug19)
 * Last modified : 9/21/21
 */

#include "syscall_wrapper.h"


/**
 * Wrapper for the fork sys call to implicitly handle any error.
 * @return child pid for the calling parent. 0 for the child. 
 */
int 
Fork(void)
{
    int pid;

    if ((pid = fork()) < 0)
    {
        perror("Fork error");
        exit(errno);
    }
    return pid;
}

/**
 * Wrapper for the pipe sys call to implicitly handle any error.
 * @param pipe_end array to be assigned with read end and write 
 *  end of the pipe.
 */
void 
Pipe(int *pipe_end)
{
    if (pipe(pipe_end) == -1)
    {
        perror("Pipe error");
        exit(errno);
    }
}

/**
 * Wrapper for the write sys call to implicitly handle any error.
 * @param fd file descriptor.
 * @buf buffer to be written to.
 * @n number of byte to be written to.
 */
void 
Write(int fd, const void *buf, size_t n) 
{
    if (write(fd, buf, n) == -1)
    {
        perror("Write error");
    }
}

/**
 * Wrapper for the dup2 sys call to implicitly handle any error.
 * @param old_fd referencing file descriptor.
 * @param new_fd file descriptor that will be pointing to the same 
 *  reference as old_fd.
 */
void 
Dup2(int old_fd, int new_fd)
{
    if (dup2(old_fd, new_fd) == -1)
    {
        perror("Dup2 error");
        exit(errno);
    }
}

/**
 * Wrapper for the wait sys call to implicitly handle any error.
 * @param statusp returning status of the child.
 */
void 
Wait(int statusp)
{
    if (wait(statusp) == -1)
    {
        perror("Wait error");
        exit(errno);
    }
}
   
void
Chdir(const char *path) 
{
    if (!chdir(path)) {
        perror("cd error");
    }
}

int
Open(char *filename) 
{
  int fd = open(filename);
  if (fd < 0) {
      perror("Open error");
      exit(errno);
  }
  return fd;
}

bool
Create(const char *filename, unsigned initial_size)
{
  bool success = create(filename, initial_size);
  if (!success)
    {
      perror("Create error");
      exit(errno);
    }
  return success;
}

