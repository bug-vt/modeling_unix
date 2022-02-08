#include "threads/scheduler.h"
#include "threads/cpu.h"
#include "threads/interrupt.h"
#include "list.h"
#include "threads/spinlock.h"
#include <debug.h>
#include "devices/timer.h"
#include <stdio.h> // debugging
/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */

/* Functions for Virtual Runtime calculations */
static inline int64_t max (int64_t x, int64_t y) {
    return x > y ? x : y;
}
static inline int64_t min (int64_t x, int64_t y) {
    return x < y && x != 0 ? x : y;
}

static int64_t calc_ideal_runtime(struct ready_queue *, struct thread *);
static int64_t calc_vruntime(struct thread * t, int64_t bonus); 

static int64_t queue_total_weight (struct ready_queue *);
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
  curr_rq->last_time = 0;
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
  if (!initial && rq_to_add->curr != NULL)
    {
      rq_to_add->curr->timer_stop = timer_gettime ();
      rq_to_add->curr->vruntime = calc_vruntime (rq_to_add->curr, 20);
      rq_to_add->curr->timer_start = timer_gettime ();
      set_min_vruntime (rq_to_add);
      t->vruntime = calc_vruntime (t, 0);
      t->vruntime = max(t->vruntime, rq_to_add->min_vruntime - 20000000);
    }
  else if (initial && rq_to_add->curr != NULL)
    {
      rq_to_add->curr->timer_stop = timer_gettime ();
      rq_to_add->curr->vruntime = calc_vruntime (rq_to_add->curr, 20);
      if (rq_to_add->curr->timer_stop - rq_to_add->curr->timer_start)
        {
          set_min_vruntime (rq_to_add);
        }
      rq_to_add->curr->timer_start = timer_gettime ();
      t->vruntime = rq_to_add->min_vruntime ? 
                    rq_to_add->min_vruntime : 
                    rq_to_add->curr->vruntime;
    }

  list_push_back (&rq_to_add->ready_list, &t->elem);
  rq_to_add->nr_ready++;

  /* CPU is idle */
  if (!rq_to_add->curr || initial == 0)
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
  current->timer_stop = timer_gettime ();

  current->vruntime = calc_vruntime(current, 20); 

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

  struct thread *ret = find_thread (curr_rq);
  list_remove (&ret->elem);
  ret->timer_start = timer_gettime ();
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
  int64_t ideal_runtime = calc_ideal_runtime(curr_rq, current);
  int64_t current_time = timer_gettime();
  curr_rq->thread_ticks += current_time - curr_rq->last_time;
  curr_rq->last_time = current_time;
  /* Enforce preemption. */
  if (curr_rq->thread_ticks >= ideal_runtime)
    {
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
  current->timer_stop = timer_gettime ();
}

/* Function that sets the min_vruntime */
void
set_min_vruntime (struct ready_queue *rq)
{
  int64_t min_vruntime = INT64_MAX;

  if (rq->curr != NULL)
    {
      min_vruntime = min (rq->curr->vruntime, min_vruntime);
    }

  struct thread * t;
  list_for_each_entry(t, &rq->ready_list.head, elem)  
    {
      min_vruntime = min (t->vruntime, min_vruntime);
    }

  rq->min_vruntime = min_vruntime == INT64_MAX ? 0 : min_vruntime;
}

/* OUR CODE */

/* Gets the weight of the ready queue, not including the
 * running thread.
 */
int64_t
queue_weight (struct ready_queue *rq)
{
  int64_t total_weight = 0;
  struct thread * t;
  list_for_each_entry(t, &rq->ready_list.head, elem)
    {
      total_weight += prio_to_weight[t->nice + 20];
    }
  return total_weight;
}

/*  */
static int64_t
queue_total_weight (struct ready_queue *rq)
{
  int64_t total_weight = 0;

  if (rq->curr != NULL)
    {
      total_weight += prio_to_weight[rq->curr->nice + 20];
    }
  struct thread * t;
  list_for_each_entry(t, &rq->ready_list.head, elem) 
    {
      total_weight += prio_to_weight[t->nice + 20];
    }

  return total_weight == 0 ? 1 : total_weight;
}


static struct thread *
find_thread (struct ready_queue * rq)
{
  struct thread * t_front = list_entry(
                                list_front (&rq->ready_list), 
                                struct thread, 
                                elem
                            );
  struct thread * t;
  list_for_each_entry(t, &rq->ready_list.head, elem) 
    {
      if (t->vruntime < t_front->vruntime ||
         (t->vruntime == t_front->vruntime && t->tid < t_front->tid)) 
        {
          t_front = t;
        }
    }
  return t_front;
}
static int64_t calc_vruntime(struct thread * t, int64_t bonus)
{
  int64_t d = t->timer_stop - t->timer_start;
  int64_t w0 = prio_to_weight[NICE_DEFAULT + bonus];
  int64_t w = prio_to_weight[t->nice + bonus];
  return t->vruntime + d * w0 / w;
}
static int64_t calc_ideal_runtime(struct ready_queue * rq, struct thread * curr)
{
  int64_t nanos = 4000000;
  int64_t n = rq->curr != NULL ? 
          rq->nr_ready + 1 : 
          rq->nr_ready;
  int64_t w = prio_to_weight[curr->nice + 20];
  int64_t s = queue_total_weight (rq);
  return nanos * n * w / s;
}

/* Returns the given thread's weight
 */
uint32_t
getThreadWeight(struct thread* t)
{
  return prio_to_weight[t->nice + 20];
}
