#include <syscall.h>
#include <stdio.h>

int
main (void)
{
  printf ("wc-test begin.\n");
  int p[2];
  int p2[2];
  pipe (p);
  pipe (p2);
  int pid = fork ();
  char buffer[20];

  if (pid == 0)
    {
      dup2 (p[1], STDOUT_FILENO);
      close (p[0]);
      close (p[1]);
      close (p2[0]);
      close (p2[1]);
      exec2 ("echo abc");
      printf ("(child) should not reach here.\n");
    }
  else if (pid > 0)
    {
      int pid2 = fork ();
      if (pid2 == 0)
        {
          dup2 (p[0], STDIN_FILENO);
          dup2 (p2[1], STDOUT_FILENO);
          close (p[0]);
          close (p[1]);
          close (p2[0]);
          close (p2[1]);
          exec2 ("wc -c");
          printf ("(child) should not reach here.\n");
        }
      else if (pid2 > 0)
        {
          int read_size = read (p2[0], buffer, 20);
          if (read_size == 4)
            printf ("read correct amount of bytes.\n");
          else
            printf ("read incorrect amount of bytes.\n");
          printf ("(parent) wc-test end.\n");
        }
    }
  else
    printf ("fork error.\n");


  return EXIT_SUCCESS;
}