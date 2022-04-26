#include <syscall.h>
#include <stdio.h>

int
main (void)
{
  printf ("dup begin.\n");
  create ("dup_fd", 0);
  int fd = open ("dup_fd");
  char buffer[20];

  dup2 (fd, 7);
  write (fd, "hello ", 6);
  write (7, "world\n", 6);

  seek (fd, 0);
  int read_bytes = read (fd, buffer, 20);
  if (read_bytes != 12)
    {
      printf ("Expected to read 12, but read %d\n", read_bytes);
      exit (-1);
    }

  printf ("%s", buffer);

  printf ("dup end.\n");

  return EXIT_SUCCESS;
}
