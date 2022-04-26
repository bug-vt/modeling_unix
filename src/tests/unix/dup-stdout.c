#include <syscall.h>
#include <stdio.h>

int
main (void)
{
  printf ("dup-stdout begin.\n");
  const int NOT_STDOUT = 3;

  dup2 (STDOUT_FILENO, NOT_STDOUT);
  write (NOT_STDOUT, "hello world\n", 12);

  printf ("dup-stdout end.\n");

  return EXIT_SUCCESS;
}
