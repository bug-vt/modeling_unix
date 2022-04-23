#include <syscall.h>
#include <stdio.h>

int
main (void)
{
  printf ("pipe begin.\n");
  int p[2];
  char buffer[20];

  pipe (p);
  int pid = fork();
  if (pid == 0)
    {
      dup2 (p[0], STDIN_FILENO);
      close (p[0]);
      close (p[1]);
      int read_bytes = read (STDIN_FILENO, buffer, 20);
      if (read_bytes != 12)
        {
          printf ("Expected to read 12, but read %d\n", read_bytes);
          exit (-1);
        }

      write (STDOUT_FILENO, buffer, 20);
      printf ("(child) pipe end.\n");
    }
  else if (pid > 0)
    {
      close (p[0]);
      int written_bytes = write (p[1], "hello world\n", 20);
      if (written_bytes != 12)
        {
          printf ("Expected to written 12, but written %d\n", written_bytes);
          exit (-1);
        }
      close (p[1]);

      wait (pid);
      printf ("(parent) pipe end.\n");
    }
  else
    printf ("fork error.\n");


  return EXIT_SUCCESS;
}
