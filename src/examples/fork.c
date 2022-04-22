#include <syscall.h>
#include <stdio.h>

int
main ()
{
  printf ("Before fork\n");
  int pid = fork ();
  if (pid > 0) 
    {
      printf ("parent: child=%d\n", pid);
      int status = wait (pid);
      printf ("child %d is done. Exited with %d\n", pid, status);
    }
  else if (pid == 0)
    {
      printf ("child: exiting\n");
      exit (0);
    }
  else
    printf ("fork error\n");

  return EXIT_SUCCESS;
}
