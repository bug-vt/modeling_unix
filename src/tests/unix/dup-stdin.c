#include <syscall.h>
#include <stdio.h>

int
main (void)
{
  printf ("dup-stdin begin.\n");
  create ("dup_fd", 0);
  int fd = open ("dup_fd");
  char buffer[20];

  dup2 (fd, STDIN_FILENO);
  write (fd, "hello world\n", 12);

  seek (STDIN_FILENO, 0);
  int read_bytes = read (STDIN_FILENO, buffer, 20);
  if (read_bytes != 12)
    {
      printf ("Expected to read 12, but read %d\n", read_bytes);
      exit (-1);
    }

  printf ("%s", buffer);

  printf ("dup-stdin end.\n");

  return EXIT_SUCCESS;
}
