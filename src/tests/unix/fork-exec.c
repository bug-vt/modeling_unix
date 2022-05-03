#include <syscall.h>
#include <stdio.h>

int
main (void)
{
  printf ("fork-exec begin.\n");
  int pid = fork ();
  if (pid == 0)
    {
      close (STDIN_FILENO);
      create ("input.txt", 0);
      int fd = open ("input.txt");
      write (fd, "hello world\n", 12);
      seek (fd, 0);

      exec2 ("cat");
      printf ("(child) should not reach here.\n");
    }
  else if (pid > 0)
    {
      wait (pid);
      printf ("(parent) fork-exec end.\n");
    }
  else
    printf ("fork error.\n");


  return EXIT_SUCCESS;
}
