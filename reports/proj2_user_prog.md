<link rel="stylesheet" href="styles.css">

# Project 2: User programs design documentation v2
#### CS 4284, Virginia Tech
#### Bug Lee (Chungeun Lee)
#### bug19@vt.edu
#### February 25, 2022

## Introduction
Addition from project 2 has enabled Pintos OS to support user programs. Initial discussion and introduction of project 2 from the lecture was a helpful first step to break down the big complex project into more comprehendible tasks. As a result, the project was broken down into five different parts: argument passing, syscall infrastructure, user memory access, process control syscalls, and file system syscalls. As with project 1, understanding the provided base code was an important first step to recognizing what base Pintos was capable of and what it did not support yet. Therefore, the following sections each start with describing what was already there. Then, it moves on to what design decision we have made and explains the reason behind it.

## Argument passing
The provided base code already has done the heavy lifting related to creating a user process and loading the requested executable into the memory. However, it interpreted the executable name and arguments as one giant string. Thus, we had decided to parse and push arguments to the stack by modifying the ```setup_stack()```. The following describes the step we have taken. Note that push operation was implemented by subtracting numbers of bytes to the stack pointer (esp). 
1. Push each argument resulting from ```strtok_r()``` to stack in the parsed order. Once all arguments are pushed, the last argument would be at the top of the stack followed by second to last and so on. Save the stack pointer address that points to the last argument.
2. Add appropriate bytes of padding to ensure word alignment. 
3. Push null sentinel, ```argv[n]```.
4. Push address of each argument string in right-to-left order. This was done by using the save pointer that points to the last argument from step 1. So the address of the last argument will be pushed first to the stack, ```argv[n-1]```. Then, the next argument address would be found by adding an offset based on the string length of the last argument. After that, the second to last argument will be pushed to the stack, ```argv[n-2]```. This continues until the address of the first argument gets pushed to the stack, ```argv[0]```.
5. Push ```argv```, the address of ```argv[0]```.
6. Push ```argc```, the number of arguments.
7. Push fake return address.

<div style="page-break-after: always;"></div>

## Syscall infrastructure
In the base code, the user process can request service to the kernel using syscall library, ```lib/user/syscall.c```, which invokes the trap/interrupt. Then, it switches to kernel mode, and the user would expect that syscall interrupt handler would provide appropriate service back to the user. However, the provided base code simply exits the user process. Therefore, we have modified syscall handler to service the interrupt based on the syscall number obtained by the user stack pointer, passed through the interrupt frame pointer. Also, based on the syscall number, we knew how many arguments were passed from the user to the stack. Our original design used a switch statement to identify then dispatch to an appropriate syscall subroutine. This was later changed to use a function pointer table, mimicking what Linux does to handle syscalls. We think that this approach makes it easier to add a new syscall without sacrificing code readability compared to the switch statement implementation.

## User memory access
The base code provided useful functions to help identify whether the user-provided address was valid. We used the approach that verifies the validity of the user-provided pointer before dereferencing it. To do so, the ```is_user_vaddr()``` and ```pagedir_get_page()``` was used. We implemented a ```verify_ptr()``` to check if the given address is below the kernel virtual address space and valid page table mapping exists for the requested process. On the other hand, the page fault and page fault handler were also given in the base code. We have made a minor modification inside ```exception.c``` to terminate the process with exit code -1 if the page fault was occurred by the user code.  
Once the above implementations were done, verifying the validity of arguments from the stack become trivial. Our approach was to verify and copy the argument byte by byte to the kernel space. Although most of the arguments were verified this way, the buffer pointer used in the read/write syscall needed another strategy. Because the length of the buffer can be as large as the disk can support it, there might not be enough space in the kernel for a copy. In addition, we observed that performance suffers for byte by byte verification for a large buffer. So instead, the validity of the buffer is verified page by page without making a copy. The end of the buffer also gets verified in case the buffer is not page-aligned. This still results in a correct verification since the buffer can be visualized as contiguous pages of memory in virtual address space. 

## Process control syscalls
In the given base code, the parent and the child process could not interact. So, it was our job to make the process aware of its children that spawn from the exec syscall, wait for its child to exit in wait syscall, and inform its parent when the child exits in exit syscall. The shared data structure, ```proc_trace```, was added to each process and acts as a medium for the parent and its child to deliver certain information to each other even after their death. Also, each process now keeps track of its children by having a trace list.  
Now, with the use of the shared data ```proc_trace```, the interaction was coordinated with the use of semaphore. One interaction is that parent must wait for the child to signal its load status during exec syscall. This was implemented by first locating the newly created child from the list of children and calling ```sema_down()``` in the parent process. Then the child process would signal its parent at the end of the ```load()```. The child still signals its parent even if it fails to load, but in that case, the parent would wait for the child to become a zombie then reap the child (this strategy is discussed below). Another interaction is that parent must wait for the child to signal its exit status during wait syscall. It follows the same logic from exec syscall where the only difference is that the child would signal the parent at the end of its exit routine.  
However, ```proc_trace``` introduced the potential for memory leak and race condition between the parent and its child. To ensure all the memories are reclaimed and the shared data are safe from the race bugs, we used the following strategy:  
1. Each trace keeps track of its reference count. The trace can be referenced by the parent, the child, both the parent and the child, or none (if both exited). When the trace reaches reference count 0, it needs to free its resource. Since both the parent and the child could access/modify reference count concurrently, a lock must be used.
2. It is the parent's responsibility to free the resource from a zombie child, a trace with only the parent's reference. This applies when the parent waits for her child to exits (wait syscall or load failure), or when the parent exits.
3. If the child is an orphan, a trace with only the child's reference, then it is the child's responsibility to free its resource when exit.  

In addition to the freeing ```proc_trace```, all the open files for the process have to be closed upon exit. Also, the code was adjusted to disable the write permission of the loaded file and close the loaded file during exit instead of the end of the load. Knowing that closing the file re-enable the write permission, it was simple to control the write permission of the running executable file. This was added after the implementation of the file descriptor table, which will be discussed next.   

## File system syscalls
A limited but completed file system was provided from the base code. All the syscall related to the file system were implemented as a wrapper with a global lock. That said, our main job was to create a file descriptor table that provide another layer of abstraction for the user process to interact with the file system. Originally we had implemented a file descriptor table using a doubly linked list. However, we later decided to switch to array base file descriptor table for the following reasons:
1. Minimal overhead for each file descriptor.  
   The original linked list implementation required 16 bytes for each files descriptor: ```fd``` (4 bytes), ```struct file *``` (4 bytes), and ```struct list_elem``` (8 bytes). In contrast, the array-based implementation only requires ```struct file *``` (4 bytes) for each file descriptor.
2. Acceptable memory allocation size for file descriptor table.  
   We conclude that allocating a page for the file descriptor table, thus a maximum of 1024 file descriptors, is more than enough for a process without adding much burden to the kernel heap.
3. Faster access time.  
   Although insertion time would be slower than the linked list, faster access time is more desirable since the process would much more frequently access the file descriptor than inserting a new one to the table.
4. Simpler file descriptor assigning scheme.  
   Following the POSIX standard for assigning the lowest available file descriptor was a trivial task for the array-based implementation. It would simply iterate over the table and locate the first empty spot. In contrast, a more sophisticated algorithm was needed for the linked list based file descriptor table.
