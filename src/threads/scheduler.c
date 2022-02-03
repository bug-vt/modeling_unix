#include "threads/scheduler.h"
#include "threads/cpu.h"
#include "threads/interrupt.h"
#include "list.h"
#include "threads/spinlock.h"
#include <debug.h>
#include <stdio.h> // debugging
/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */

/* Functions for Virtual Runtime calculations */
static inline int64_t max (int64_t x, int64_t y) {
    return x > y ? x : y;
}
static void find_min_vruntime (struct ready_queue *);
static int32_t queue_total_weight (struct ready_queue *);
static struct thread *find_thread (struct ready_queue *);

/* Table used to map a nice value to weight */
static const uint32_t prio_to_weight[40] =
  {
    /* -20 */    88761, 71755, 56483, 46273, 36291,
    /* -15 */    29154, 23254, 18705, 14949, 11916,
    /* -10 */    9548, 7620, 6100, 4904, 3906,
    /*  -5 */    3121, 2501, 1991, 1586, 1277,
    /*   0 */    1024, 820, 655, 526, 423,
    /*   5 */    335, 272, 215, 172, 137,
    /*  10 */    110, 87, 70, 56, 45,
    /*  15 */    36, 29, 23, 18, 15,
  };

/*
 * In the provided baseline implementation, threads are kept in an unsorted list.
 *
 * Threads are added to the back of the list upon creation.
 * The thread at the front is picked for scheduling.
 * Upon preemption, the current thread is added to the end of the queue
 * (in sched_yield), creating a round-robin policy if multiple threads
 * are in the ready queue.
 * Preemption occurs every TIME_SLICE ticks.
 */

/* Called from thread_init () and thread_init_on_ap ().
   Initializes data structures used by the scheduler. 
 
   This function is called very early.  It is unsafe to call
   thread_current () at this point.
 */
void
sched_init (struct ready_queue *curr_rq)
{
  list_init (&curr_rq->ready_list);
  curr_rq->min_vruntime = 0;
}

/* Called from thread.c:wake_up_new_thread () and
   thread_unblock () with the CPU's ready queue locked.
   rq is the ready queue that t should be added to when
   it is awoken. It is not necessarily the current CPU.

   If called from wake_up_new_thread (), initial will be 1.
   If called from thread_unblock (), initial will be 0.

   Returns RETURN_YIELD if the CPU containing rq should
   be rescheduled when this function returns, else returns
   RETURN_NONE */
enum sched_return_action
sched_unblock (struct ready_queue *rq_to_add, struct thread *t, int initial UNUSED)
{
  list_push_back (&rq_to_add->ready_list, &t->elem);
  rq_to_add->nr_ready++;
  t->cpu_consumed = 0;
  //t->times_used ++;

  /* CPU is idle */
  if (!rq_to_add->curr)
    {
      return RETURN_YIELD;
    }
  return RETURN_NONE;
}

/* Called from thread_yield ().
   Current thread is about to yield.  Add it to the ready list

   Current ready queue is locked upon entry.
 */
void
sched_yield (struct ready_queue *curr_rq, struct thread *current)
{
  list_push_back (&curr_rq->ready_list, &current->elem);
  curr_rq->nr_ready ++;
  /* Cleans up CFS calculations */
  current->cpu_consumed = 0;
  //current->times_used ++;
}

/* Called from next_thread_to_run ().
   Find the next thread to run and remove it from the ready list
   Return NULL if the ready list is empty.

   If the thread returned is different from the thread currently
   running, a context switch will take place.

   Called with current ready queue locked.
 */
struct thread *
sched_pick_next (struct ready_queue *curr_rq)
{
  if (list_empty (&curr_rq->ready_list))
    return NULL;

  //struct thread *ret = list_entry(list_pop_front (&curr_rq->ready_list), struct thread, elem);
  struct thread *ret = find_thread(curr_rq);
  list_remove(&ret->elem);
  curr_rq->nr_ready--;
  return ret;
}

/* Called from thread_tick ().
 * Ready queue rq is locked upon entry.
 *
 * Check if the current thread has finished its timeslice,
 * arrange for its preemption if it did.
 *
 * Returns RETURN_YIELD if current thread should yield
 * when this function returns, else returns RETURN_NONE.
 */
enum sched_return_action
sched_tick (struct ready_queue *curr_rq, struct thread *current UNUSED)
{
  /* Do processes/calculations for CFS */
  int64_t vruntime_0 = !current->times_used ?
    curr_rq->min_vruntime :
    max(current->vruntime, curr_rq->min_vruntime - 20000000);

  current->vruntime = !current->cpu_consumed ?
    vruntime_0 + current->cpu_consumed ++ * prio_to_weight[NICE_DEFAULT] / prio_to_weight[current->nice] :
    current->vruntime + current->cpu_consumed ++ * prio_to_weight[NICE_DEFAULT] / prio_to_weight[current->nice];

  find_min_vruntime (curr_rq);
  int ready_or_running = curr_rq->curr != NULL ? curr_rq->nr_ready + 1 : curr_rq->nr_ready;
  //printf("current thread: %s, vruntime: %lld, min_vruntime: %lld,", current->name, current->vruntime, curr_rq->min_vruntime);
  //printf(" times used: %d, cpu consumed: %lld,", current->times_used, current->cpu_consumed);
  //printf(" runtime: %d\n", TIME_SLICE * ready_or_running * prio_to_weight[current->nice] / queue_total_weight (curr_rq));
  // if (queue_total_weight (curr_rq) == 0) {
  //   printf("divide by 0, %lld\n", curr_rq->min_vruntime);
  // }
  /* Enforce preemption. */
  if (++curr_rq->thread_ticks >= TIME_SLICE * ready_or_running * prio_to_weight[current->nice] / queue_total_weight (curr_rq))
    {
      /* Cleans up CFS calculations */
      current->cpu_consumed = 0;
      current->times_used ++;
      /* Start a new time slice. */
      curr_rq->thread_ticks = 0;
      return RETURN_YIELD;
    }
  return RETURN_NONE;
}

/* Called from thread_block (). The base scheduler does
   not need to do anything here, but your scheduler may. 

   'cur' is the current thread, about to block.
 */
void
sched_block (struct ready_queue *rq UNUSED, struct thread *current UNUSED)
{
  /* Cleans up CFS calculations */
  current->cpu_consumed = 0;
  //current->times_used ++;
  ;
}

/* Function that finds the min_vruntime value and sets it up */
static void
find_min_vruntime (struct ready_queue *rq)
{
  int64_t min_vruntime = 0;

  if (rq->curr != NULL)
    {
      min_vruntime = rq->curr->vruntime;
    }

  for (struct list_elem * e = list_begin (&rq->ready_list); e != list_end (&rq->ready_list); e = list_next (e))
    {
      struct thread * t = list_entry (e, struct thread, elem);
      if (t->vruntime < min_vruntime)
        {
          min_vruntime = t->vruntime;
        }
    }

  rq->min_vruntime = min_vruntime;
}

/*  */
static int32_t
queue_total_weight (struct ready_queue *rq)
{
  int32_t total_weight = 0;

  if (rq->curr != NULL)
    {
      total_weight += prio_to_weight[rq->curr->nice];
    }

  for (struct list_elem * e = list_begin (&rq->ready_list); e != list_end (&rq->ready_list); e = list_next (e))
    {
      struct thread * t = list_entry (e, struct thread, elem);
      total_weight += prio_to_weight[t->nice];
    }

  return total_weight == 0 ? 1 : total_weight;
}

/*  */
static struct thread *
find_thread (struct ready_queue * rq)
{
  struct thread * th = list_entry(list_front (&rq->ready_list), struct thread, elem);
  for (struct list_elem * e = list_begin (&rq->ready_list); e != list_end (&rq->ready_list); e = list_next (e))
    {
      struct thread * t = list_entry (e, struct thread, elem);
      //printf("%s\n", t->name);
      if (t->vruntime < th->vruntime)
        {
          th = t;
        }
      else if (t->vruntime == th->vruntime && t->tid < th->tid)
        {
          th = t;
        }
    }
  return th;
}