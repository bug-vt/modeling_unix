SRCDIR = ../..

# Test programs to compile, and a list of sources for each.
# To add a new test, put its name on the PROGS list
# and then add a name_SRC line that lists its source files.
PROGS = fork fork2 dup dup-stdin dup-stdout pipe fork-exec fork-dup-exec \
		sbrk malloc pipe-err pipe-err2 jobserver wc-test

# Should work in project 5.
fork_SRC = fork.c
fork2_SRC = fork2.c
dup_SRC = dup.c
dup-stdin_SRC = dup-stdin.c
dup-stdout_SRC = dup-stdout.c
pipe_SRC = pipe.c
fork-exec_SRC = fork-exec.c
fork-dup-exec_SRC = fork-dup-exec.c
sbrk_SRC = sbrk.c
malloc_SRC = malloc.c
pipe-err_SRC = pipe-err.c
pipe-err2_SRC = pipe-err2.c
jobserver_SRC = jobserver.c
jobserver_SRC += syscall_wrapper.c
wc-test_SRC = wc-test.c

include $(SRCDIR)/Make.config
include $(SRCDIR)/Makefile.userprog
