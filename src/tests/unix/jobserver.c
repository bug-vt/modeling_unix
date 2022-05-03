#include <stdio.h>
#include <syscall.h>
#include <string.h>
#include "syscall_wrapper.h"

#define MAX_WORKER 4

struct summary
  {
    int64_t write_time;
    int64_t total_time;
    int64_t results[3];
  };

static int64_t test_read_write_overlap (char *input, int size);
static void test_with_jobserver (char *input, int size, int num_workers);
static void print_summary (struct summary *);

static char *input_file = "mobydick.txt";

int
main (void) 
{
  printf ("jobserver begin\n");

  struct summary *summary = malloc (sizeof (struct summary));
  /* Load input into memory. 
     This is to avoid timing I/O access during test. */
  int fd = Open (input_file);
  int size = filesize (fd);
  char *input = malloc (size + 1);
  read (fd, input, size);
 
  printf ("--Testing read/write overlap--\n");
  int64_t start = times ();
  summary->write_time = test_read_write_overlap (input, size);
  int64_t end = times ();
  summary->total_time = end - start;
  printf ("--Test finish--\n");

  int i = 0;
  /* Test increase in speed with multiple workers. */
  for (int n = 1; n <= MAX_WORKER; n *= 2, i++)
    {
      printf ("--Performing task thorugh jobserver--\n");
      printf ("--%d worker(s)--\n", n);
      start = times ();
      test_with_jobserver (input, size, n);
      end = times ();
      printf ("Total clock ticks: %lld\n", end - start);
      summary->results[i] = end - start;
      printf ("--Task finish--\n");
    }
  
  print_summary (summary);

  printf ("jobserver end\n");
  return EXIT_SUCCESS;
}


static int64_t
test_read_write_overlap (char *input, int size)
{
  int pipe[2];
  Pipe (pipe);
  int pid = Fork ();
  /* Child */
  if (pid == 0)
    {
      /* Take input from Parent. */
      Dup2 (pipe[0], STDIN_FILENO);
      close (pipe[0]);
      close (pipe[1]);
      close (STDOUT_FILENO);
      exec2 ("xor_cipher");
    }
  /* Parent */
  close (pipe[0]);

  /* Measure write duration. */
  int64_t start = times ();
  Write (pipe[1], input, size);
  int64_t end = times ();
  close (pipe[1]);

  Wait (pid);
  printf ("Write: clock ticks: %lld\n", end - start);

  return end - start;
}

static void 
test_with_jobserver (char *input, int size, int num_workers)
{
  int pid = Fork ();
  /* Manager */
  if (pid == 0)
    {
      /* Setting up pipes */
      int worker_idx = 0;
      int pipe[2];
      Pipe (pipe);

      int pids[num_workers];
      /* Assign task to each worker. */
      for (int i = 0; i < num_workers; i++)
        {
          pids[worker_idx] = Fork ();
          /* Worker */
          if (pids[worker_idx] == 0)
            {
              /* Take input from Manager. */
              Dup2 (pipe[0], STDIN_FILENO);
              close (pipe[0]);
              close (pipe[1]);
              /* stdout is close to avoid including I/O
                 time writing to console. */
              close (STDOUT_FILENO);
              exec2 ("xor_cipher");
            }
        }
      /* Manager */
      /* Give output to workers. */
      Dup2 (pipe[1], STDOUT_FILENO);
      close (pipe[0]);
      close (pipe[1]);
      Write (STDOUT_FILENO, input, size);
      exit (0);
    }
  
  Wait (pid);
}

static void
print_summary (struct summary *summary)
{
  printf ("-----------------------------------\n");
  printf ("| Read/Write overlap test result: |\n");
  printf ("-----------------------------------\n");
  printf ("| Write duration: |   %lld ticks  |\n", summary->write_time);
  printf ("| Total duration: |   %lld ticks  |\n", summary->total_time);
  printf ("-----------------------------------\n");
  printf ("|     Jobserver test result:      |\n");
  printf ("-----------------------------------\n");
  printf ("| 1 worker:  |     %lld ticks     |\n", summary->results[0]);
  printf ("| 2 workers: |     %lld ticks     |\n", summary->results[1]);
  printf ("| 4 workers: |     %lld ticks     |\n", summary->results[2]);
  printf ("-----------------------------------\n");
}
