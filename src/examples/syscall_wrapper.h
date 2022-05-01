
/**
 * syscall_wrapper.h
 * Author : Bug Lee (bug19)
 * Last modified : 9/18/21
 *
 * This module contains wrapper functions for system calls.
 * This technique was adapted from the course textbook.
 */

#ifndef SYSCALL_WRAPPER
#define SYSCALL_WRAPPER


#include <stdio.h>
#include <stdlib.h> 
#include <syscall.h>
#include <errno.h>

#define READ_END 0
#define WRITE_END 1

//sys call wrapper functions
int Fork(void);
void Pipe(int *pipe_end);
void Write(int fd, const void *buf, size_t n);
void Dup2(int old_fd, int new_fd);
void Wait(int statusp);
void Chdir(const char *path);
int Open(char *filename);

#endif
