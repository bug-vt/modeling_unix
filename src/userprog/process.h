#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/interrupt.h"

tid_t process_fork (struct intr_frame *);
tid_t process_execute (const char *file_name);
void process_start (void *file_name) NO_RETURN;
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
