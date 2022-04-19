#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "filesys/file.h"
#include "threads/synch.h"

/* States in a thread's life cycle. */
enum thread_status
{
  THREAD_RUNNING,       /* Running thread. */
  THREAD_READY,         /* Not running but ready to run. */
  THREAD_BLOCKED,       /* Waiting for an event to trigger. */
  THREAD_DYING          /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */
#define THREAD_NAME_MAX 16
/* Thread priorities. */
#define NICE_MIN -20                    /* Highest priority. */
#define NICE_DEFAULT 0                  /* Default priority. */
#define NICE_MAX 19                     /* Lowest priority. */
#define FD_MAX 1024                     /* File descriptor capacity per process */

/* Share data between the parent and the child process. */
struct maternal_bond 
{
  struct list_elem elem;   /* List element for children list. */
  tid_t tid;               /* The tid of the child process */
  struct semaphore load;   /* The semaphore that's used for loading. */
  struct semaphore exit;   /* The semaphore that's used for exiting */
  int reference_counter;   /* This describe the bond relationship
                              between the parent and the child:
                              2 = both parent and child are alive.
                              1 = The current process is either zombie
                                  or orphan.
                              0 = both parent and the child are dead. */
  struct spinlock lock;    /* The spinlock that protects the 
                              reference counter */
  int status;              /* Holds the exit status of the child process */
  bool load_fail;          /* Indication for load failure during
                              process start-up. */
};

/* Struct that contains the table of fd mappings */
struct fd_table 
{
  /* Array that stores the file descriptor mappings */
  struct file *fd_to_file[FD_MAX]; 
};

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */

struct thread
{
  /* Owned by thread.c. */
  tid_t tid; /* Thread identifier. */
  enum thread_status status; /* Thread state. */
  char name[THREAD_NAME_MAX]; /* For thread: Name (for debugging purposes). 
                                 For process: Executable name w/o arguments. */
  uint8_t *stack; /* Saved stack pointer. */
  int nice; /* Nice value. */
  struct list_elem allelem; /* List element for all threads list. */

  struct cpu *cpu; /* Points to the CPU this thread is currently bound to.
                      thread_unblock () will add a thread to the rq of
                      this CPU.  A load balancer needs to update this
                      field when migrating threads.
                    */

  /* Shared between thread.c and synch.c. */
  struct list_elem elem; /* List element. */

#ifdef USERPROG
  /* Owned by userprog/process.c. */
  uint32_t *pagedir; /* Page directory. */
#endif
  
  /* OUR CODE */
  /* Used for CFS in scheduler.c */
  int64_t vruntime; /* Virtual Runtime */
  /* CPU consumption */
  int64_t timer_start; /* The start of CPU consumption */
  int64_t timer_stop; /* The end of CPU consumption */

  /* Used for process.c */
  struct maternal_bond *bond; /* shared data between current process
                                 and its parent process. */
  struct list children; /* List of bond between current process and
                           its children. */
  struct file *file; /* Executable file that the current process 
                        is executing. */
  /* Used for syscall.c */
  struct fd_table *fd_table; /* file descriptor table */

  /* File name for syscalls that uses them */
  char *syscall_arg;

  struct dir *current_dir; /* Current working directory. */

  /* Owned by thread.c. */
  unsigned magic; /* Detects stack overflow. */
};

void thread_init (void);
void thread_init_on_ap (void);
void thread_start_idle_thread (void);
void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (struct spinlock *);
void thread_unblock (struct thread *);
struct thread *running_thread (void);
struct thread * thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);
void thread_exit_ap (void) NO_RETURN;
/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);
int thread_get_nice (void);
void thread_set_nice (int);

#endif /* threads/thread.h */
