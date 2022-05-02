#include <stdio.h>
#include <syscall.h>
#include <string.h>
#include "syscall_wrapper.h"

#define MAX_WORKER 1

static void do_task_without_pipe (void);
static void do_task_with_pipe (int num_workers);

static char *task = "xor_cipher mobydick.txt";
static char *ref_output = "output.txt";
static char *p_output = "p-output.txt";

int
main (void) 
{
  printf ("p5-demo begin\n");
  
  int64_t start = times ();
  do_task_without_pipe ();
  printf ("clock ticks: %lld\n", times () - start);

  for (int n = 1; n <= MAX_WORKER; n *= 2)
    {
      start = times ();
      do_task_with_pipe (n);
      printf ("clock ticks: %lld\n", times () - start);
      remove (p_output);
    }

  printf ("p5-demo end\n");
  return EXIT_SUCCESS;
}


static void
do_task_without_pipe (void)
{
  printf ("--Performing task without pipe--\n");
  char *tmp_file = ".p5-demo.tmp";
  int pid = Fork ();
  /* Child: Encrypt message. */
  if (pid == 0)
    {
      close (STDOUT_FILENO);
      create (tmp_file, 0);
      Open (tmp_file);
      exec2 (task);
    }

  /* Parent: Wait for Encryption to finish. */
  Wait (pid);

  pid = Fork ();
  /* Child: Decrypt message. */
  if (pid == 0)
    {
      close (STDOUT_FILENO);
      create (ref_output, 0);
      Open (ref_output);
      close (STDIN_FILENO);
      Open (tmp_file);
      exec2 ("xor_cipher .p5-demo.tmp");
    }

  /* Parent: Wait for decryption to finish. */
  Wait (pid);
  remove (tmp_file);
  printf ("--Task finish--\n");
}

static void 
do_task_with_pipe (int num_workers)
{
  printf ("--Performing task with pipe--\n");
  printf ("--%d worker(s)--\n", num_workers);
  
  int pid = Fork ();
  if (pid == 0)
    {
      /* Setting up pipes */
      int worker_idx = 0;
      int pipe[2];
      Pipe (pipe);
      /* Setting up shared output file */
      Create (p_output, 0);
      int fd = Open (p_output);

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
              /* Redirect to stdout to shared output file. */
              Dup2 (fd, STDOUT_FILENO);
              close (fd);
              exec2 ("xor_cipher");
            }
        }
      /* Manager */
      /* Give output to workers. */
      Dup2 (pipe[1], STDOUT_FILENO);
      close (pipe[0]);
      close (pipe[1]);
      exec2 (task);
    }
  
  Wait (pid);
  printf ("--Task finish--\n");
}
