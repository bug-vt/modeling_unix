#include <syscall.h>
#include <stdio.h>
#include <string.h>

#define SM_SIZE 100
#define MID_SIZE 4000
#define BIG_SIZE 40000

void *heap_end;

static void expand_heap (int iter, int bytes);
static void fail (char *msg);

int
main (void)
{
  printf ("sbrk begin.\n");

  heap_end = sbrk (0);
  printf ("current break: %p\n", heap_end);

  expand_heap (20, SM_SIZE);

  expand_heap (10, MID_SIZE);

  expand_heap (10, BIG_SIZE);

  printf ("sbrk end.\n");

  return EXIT_SUCCESS;
}

static void
expand_heap (int iter, int bytes)
{
  for (int i = 0; i < iter; i++)
    {
      void *prev_end = sbrk (bytes);
      if (prev_end != heap_end)
        fail ("sbrk return address does not match with previous break.");
      
      heap_end = sbrk (0);
      if (prev_end + bytes != heap_end)
        fail ("sbrk did not increment heap with given size.");

      printf ("new break: %p\n", heap_end);

      /* Write to heap. Should not segfault. */
      memcpy (heap_end, "hello world", 12);
      printf ("%s\n", (char *) heap_end);
    }
}

static void
fail (char *msg)
{
  printf ("%s\n", msg);
  exit (-1);
}
