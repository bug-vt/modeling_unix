#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

/* States in a thread's life cycle. */
enum pointer_check
{
  NONE,       /* Running thread. */
  STACK,         /* Not running but ready to run. */
  STRING,       /* Waiting for an event to trigger. */
  READWRITE          /* About to be destroyed. */
};

void validate_ptr(const void *addr, enum pointer_check status, unsigned size);
void sys_exit (int status);

struct lock filesys_lock;

#endif /* userprog/syscall.h */
