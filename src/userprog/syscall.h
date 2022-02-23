#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

void validate_ptr(const void *addr);
void sys_exit (int status);

#endif /* userprog/syscall.h */
