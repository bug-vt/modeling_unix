#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

void check_user_args (void *arg);
void check_const_user_args (const void *arg);

#endif /* userprog/syscall.h */
