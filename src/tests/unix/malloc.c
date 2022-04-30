#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BIG_BLK 100000
#define ITER 100

int
main (void)
{
  printf ("malloc begin.\n");

  char *msg = "hello world";
 
  char *test = malloc (strlen (msg) + 1);
  memcpy (test, msg, strlen (msg) + 1);
  printf ("%s\n", test);
  free (test);

  for (int i = 0; i < ITER; i++)
    {
      test = malloc (BIG_BLK);
      memcpy (test, msg, strlen (msg) + 1);
      printf ("Allocated %d bytes from heap.\n", BIG_BLK);
      free (test);
      printf ("Freed %d bytes from heap.\n", BIG_BLK);
      printf ("Current break is at %p\n", sbrk (0));
    }

  printf ("malloc end.\n");

  return EXIT_SUCCESS;
}
