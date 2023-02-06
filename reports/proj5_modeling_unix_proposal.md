# Pintos Open Ended Project: The Unix Track
#### Written by Adil Sheikh, Keeling Wood, Justin Hughes, Bug Lee


## fork/exec + exec2  
Our plan is to modify current process_exec() into two functions, `process_fork()` and `process_exec`, that resemble `fork` and `execve` from Unix. During fork, the parent will be duplicated. This includes copying its virtual address space (i.e. copying all pages in the parent's supplementary page table), and all its file descriptors. Also, the current use of the fake interrupt frame and the return address will be adjusted to use the appropriate parent interrupt frame and EAX for the return address. Implementation of `exec2` or so call `posix_spawn` is also a possibility but it is currently considered as a lower priority compared to other features.

## Pipe
The current plan is to implement pipe by creating a per-process shared data between two processes. For synchronization, a consumer/producer pattern with a monitor will be used. Also, Like Linux, a pipe will hold limited capacity. For that reason, we plan to use conditional variables, read/write locks, and an array with head and tail pointers to implement pipe.

## File descriptors
To better support pipe and fork/exec, an additional layer of indirection will be added between the file descriptor and file. This way, the child process can inherit the file descriptors from its parent. The close operation will no longer guaranteed to close the file, but it instead decrement the reference count of the open file and the file will remain in memory until the reference count reaches 0. As a result, `dup2` can be implemented by incrementing the reference count of the open file. Similarly, `mmap` will also increment the reference count and the current `file_map_table` will be removed. The current hard coding of fd 0 and fd 1 will be removed.

## User heap
The virtual address manager will be added to support the user side `malloc` and `sbrk`. The initial allocation of the heap will start on the next page above the BSS segment. When there is no more free space left, the heap will grow using `sbrk`. For the scope of this project, the user heap will not shrink.

## End goal
- Demonstrate support for forking and executing the child process.
- Demonstrate support for user heap.
- Piping output from one process to another process like `wc` or `grep`.
- Implementation of `wc`, `grep`, `sleep` and enrichment of current c library if necessary.
- Demonstrate speed-up gain by using pipe and running multiple processes in parallel. This can be shown by timing the reading and writing of two processes using pipe compare to using redirection (executing in sequence). 

---
# Extra
The following section describes what we like to implement once all the above features are successfully implemented. Therefore, it is currently on our lower priority list.

## SIGKILL signal
- Unlike Unix SIGKILL, it would be un-maskable and the receiving process cannot avoid the signal.
- The additional flag will be added to the `struct thread` to indicate whether SIGKILL is pending.
- The scheduler must check the flag before changing to a different thread state and should be killed only in the running state. If the process is in the running state in another CPU, then force the process into kernel mode and check if it should be terminated. If the process is in the ready state then the scheduler would terminate the process instead of running it. Lastly, if the process is blocked, it must wait until it gets unblocked first then terminated. Also, it may need to back out until the process return from kernel to user mode.
- The interrupt handler will interpret `<Ctrl-c>` as the SIGKILL signal.
- The parent process keep track of its children through `maternal_bond`, shared data between parent and child. This create a family tree and can represent the process group. This way, SIGKILL can terminate the whole process group by traversing the tree.
- The distinction between foreground and background will be added.

