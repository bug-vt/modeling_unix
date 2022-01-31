#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <round.h>
#include <stdint.h>
#include <stdbool.h>
#include <list.h>

/* Number of timer interrupts per second. */
#define TIMER_FREQ 1000
#define NSEC_PER_SEC 1000000000

/* Struct for handling timer sleeping OUR CODE*/
struct waiting_thread
{
   // The tick this thread is waiting for
   int64_t when;
   
   // Pointer to the thread for waking up on the correct tick
   struct thread * to_wake;

   // List element for linking inside the double list
   struct list_elem elem;
};

/* Comparision function used by the list functions to order the waiting thread list OUR CODE*/
bool timer_sleep_lessThan (const struct list_elem * a, const struct list_elem * b, void * aux);

/* Function for awakening all threads waiting for current tick OUR CODE*/
void awaken_sleeping_threads (void);

void timer_init (void);
void timer_calibrate (void);

int64_t timer_ticks (void);
int64_t timer_elapsed (int64_t);

/* Sleep and yield the CPU to other threads. */
void timer_sleep (int64_t ticks);
void timer_msleep (int64_t milliseconds);
void timer_usleep (int64_t microseconds);
void timer_nsleep (int64_t nanoseconds);

/* Busy waits. */
void timer_mdelay (int64_t milliseconds);
void timer_udelay (int64_t microseconds);
void timer_ndelay (int64_t nanoseconds);

void timer_print_stats (void);

/* Set the current time */
void timer_settime(uint64_t); 

/* Return the current wall clock time in ns */
uint64_t timer_gettime (void);

#endif /* devices/timer.h */
