#include <syscall.h>
#include <stdio.h>

int
main (void)
{
  printf ("fork-dup-exec begin.\n");
  int p[2];
  pipe (p);
  int pid = fork ();

  if (pid == 0)
    {
      dup2 (p[0], STDIN_FILENO);
      close (p[0]);
      close (p[1]);
      exec2 ("cat");
      printf ("(child) should not reach here.\n");
    }
  else if (pid > 0)
    {
      close (p[0]);
      write (p[1], "hello world\n", 12);
      close (p[1]);
      wait (pid);
      printf ("(parent) fork-dup-exec end.\n");
    }
  else
    printf ("fork error.\n");


  return EXIT_SUCCESS;
}
