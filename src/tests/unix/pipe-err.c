#include <syscall.h>
#include <stdlib.h>
#include <stdio.h>

int
main (void)
{
  printf ("pipe-err begin.\n");
  int p[2];

  pipe (p);
  int pid = fork();
  if (pid == 0)
    {
      close (p[1]);
      int written = write (p[0], "This should fail!", 20);
      if (written >= 0)
        {
          printf ("(child) pipe error: should not write to read-end\n");
          exit (-1);
        }
      close (p[0]);

      printf ("(child) pipe-err end.\n");
    }
  else if (pid > 0)
    {
      int buf[100];
      close (p[0]);
      int nread = read (p[1], buf, 100);
      if (nread >= 0)
        {
          printf ("(parent) pipe error: should not read from write-end\n");
          exit (-1);
        }
      close (p[1]);
    
      int exit_code = wait (pid);
      if (exit_code < 0)
        {
          printf ("(parent) wait error: child exit with %d\n", exit_code);
          exit (-1);
        }
      printf ("(parent) pipe-err end.\n");
    }
  else
    printf ("fork error.\n");


  return EXIT_SUCCESS;
}
