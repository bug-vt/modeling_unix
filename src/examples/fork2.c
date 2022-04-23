#include <syscall.h>
#include <stdio.h>

int
main (void)
{
  printf ("fork2 begin.\n");
  create ("same_fd", 0);
  int fd = open ("same_fd");
  char buffer[20];

  int pid = fork ();
  if (pid == 0) 
    {
      write (fd, "hello ", 6);
      exit (0);
    }
  else if (pid > 0)
    {
      wait (pid);
      write (fd, "world\n", 6);
    }
  else
    printf ("fork error.\n");

  seek (fd, 0);
  int read_bytes = read (fd, buffer, 20);
  if (read_bytes != 12)
    printf ("Expected to read 12, but read %d\n", read_bytes);

  printf ("%s", buffer);

  printf ("fork2 end.\n");

  return EXIT_SUCCESS;
}
