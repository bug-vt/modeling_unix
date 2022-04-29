#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>

int
main (void)
{
  printf ("malloc begin.\n");

  char *hello = "hello world";
  
  mm_init();
  char *test = malloc (20);
  memcpy (test, hello, 15);

  printf ("%s\n", test);
  

  printf ("malloc end.\n");

  return EXIT_SUCCESS;
}
