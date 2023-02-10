<link rel="stylesheet" href="styles.css">

# Project 3: Virtual Memory design documentation
#### CS 4284, Virginia Tech
#### Bug Lee (Chungeun Lee)
#### bug19@vt.edu
#### March 26, 2022

## Introduction
In Pintos project 3, support for virtual memory has been improved. The base code already provided support for virtual memory through the use of a page table. However, the size of the physical memory was the upper limit on what OS can support at once. Fortunately, the addition of project 3 has increased this upper limit without growing the size of physical memory. That is, enabling support for user processes that can be bigger than the actual physical memory. The following sections describe what was already there (if any) and what we have added to make the virtual memory more robust.

## Frames and frame table
The provided base code managed the physical memory through the use of `kpage` and various `palloc` functions. However, it did not provide enough information to support efficient eviction. Therefore, `struct frame` and `struct list frame table` were added. The frame data structure record information that would determine whether the frame should be evicted or written back to file in addition to starting address in the physical memory it is occupying. It uses dirty, accessed, and present bits from the hardware page table. Also, instead of re-inventing the wheel, we had decided to continue using `palloc` for managing the user pool. To do so, we had to create a wrapper called `frame_alloc()` which invoke `palloc` internally. On the other hand, the frame table keeps track of frames that are currently resident inside physical memory. When the user pool becomes full, the running process can iterate through the frame table and select the frame that has not been used for a while. The selected frame would then be free/evicted from the physical memory, which now can be used by the running process. The global page replacement scheme was considered and therefore there is only one frame table and it is shared by all processes.

## Pages and supplementary page table
The pages and supplementary page table represent the valid virtual memory address space for each process. Since the virtual address space of each process should be isolated from each other, pages and the supplementary page table were designed as a per-process data structure. The addition of page data structure enabled on-demand paging. In contrast to the base code, no physical memory would be loaded into the process during the process start-up (except for the initial stack). Instead, required numbers of pages that represent specified segments defined from the ELF binary would be allocated and added to the supplementary page table during `load_segment()`. This would be later used when servicing page fault, where the validity of the faulting address is determined by whether the page that contains the faulting address exists inside the supplementary page table.  

Additionally, the supplementary page table is also responsible for freeing resources related to a page during the process exit. 
- If the page does not have any occupying frame, then it can be simply discarded. This can happen when it has not accessed/modified any data for the given virtual address space or when the page had been evicted.
- For the page that has a mapping to a physical frame, only the page that holds file mapping with the dirty bit on should be written back to the file.

## File mapping
A limited form of file mapping was added to the Pintos. The given constraint simplified the `mmap` and `munmp` syscall tremendously. Especially, since the user-provided address must be page align, it showed a similar pattern from `load_segment()` on how it needed to populate pages. Therefore, `populate_pages()` that was used for `load_segment()` was recycled to allocate pages and add to the supplementary page table. Checking the validity of virtual address space for file mapping was mostly straightforward. The most tricky part was checking if the provided address space for file mapping would overlap with existing pages. This was done by iterating over consecutive pages that would cover the entire file mapping and seeing if a page already exists in the supplementary page table.  `munmap` used a similar strategy where it iterates over consecutive pages that cover the entire file mapping and freeing each page and its occupying frame if any.

## Swap space
The project 3 test code provided an additional virtual hard disk to be used for swap space. Pintos provided the `struct block` and `struct bitmap` and most of the heavy lifting was already done for accessing/modifying the virtual hard disk. `block_read()` and `block_write()` perform read/write operation to virtual hard disk. `bitmap` was used to keep track of which sectors in the block are available and organize the swap space by the page size. Knowing that size of the bit map is the number of sectors in the virtual hard disk, finding an empty page in swap space was done by locating 8 consecutive 0 bits inside the bitmap. Similarly, reading from and writing to swap space using `block_read()`/`block_write()` had to iterate over 8 consecutive sectors so that operation is done on page bases.

## Page replacement policy
The clock (or second chance) replacement policy was used to approximate LRU. The idea was to find a frame that has not been accessed for a long duration. While iterating over the frame table, the accessed bit of the frame that is pointed by the clock hand is checked to see if it was recently accessed. If so, the accessed bit is reset to 0, and the clock hand moves on to the next frame. Otherwise, the frame with accessed bit 0 indicates that it has not been accessed for a while, which signals that the pointing frame would be a good candidate for the eviction.  
On the other hand, evicting a frame that is currently under the use of read/write syscall had to be avoided. Therefore, pinning was implemented using per-frame locks. The read/write syscalls were modified to operate on a given buffer page by page and the frame that is being read/written would be pinned during the operation. In addition, the accessing frame had to be loaded in the case when it was not a current resident in physical memory. This was to avoid the page fault while holding a `filesys_lock`. With these adjustments, the clock hand checks if the frame is pinned by some process using `lock_try_acquire()`. If so, it simply moves on to the next frame regardless of the access bit.

## Page fault handler
The page fault occurs when there is no mapping exists for the accessing virtual address to the physical address. When a page fault occurs, the process traps into the kernel and invokes the page fault handler. Inside the page fault handler, the faulting address indicates the location of the virtual address where the page fault occurred. In the base code, page fault simply meant an error. Now, the validity of the corresponding page is determined by looking up the page address that contains the faulting address at the supplementary page table. If the associate page is valid, then the frame would be allocated and loaded to the page instead of terminating the faulting process. One exception is the stack growth, where the associate page would be invalid. The following step describes the loading procedure:
- First, a frame is allocated. In our implementation, it is allocated through `frame_alloc()` from the user pool.
- If the user pool is full (`frame_alloc()` return `NULL`), then one of the frames will be selected based on the replacement policy and evicted to the swap space. The newly open spot is then allocated to the frame.
- If the frame was selected to be evicted, only the DATA, BSS, and STACK need to be stored in the swap space. Since swap space is yet another hard disk, there is no advantage of writing to swap space when the data can be read back from the file.
- Next, necessary data would be loaded into the allocated frame. If the page had been evicted, then it would load from the swap space. Otherwise, it would be loaded from the file.
- Finally, the allocated frame now can be installed on the faulting page. Once the installation is done, the user process would no longer page fault at the current faulting address the next time it tries to access it.

## Stack growth
The base code only supported the allocation of the initial stack during process start-up. Now, the page fault handler supports the stack growth. A simple heuristic was used to distinguish between invalid access and stack growth: the user cannot request access beyond 32 bytes from the stack pointer. The reason is that there are only two instructions that could grow the stack, `push` and `pusha`. `push` expand the stack pointer by 4 bytes whereas `pusha` expands the stack pointer by 32 bytes. Thus, stack growth must be within the 32 bytes from the stack pointer.

## Synchronization
Two coarse-grain locks, `frame_table_lock` and `swap_lock`, and one finer-grain lock, `page_lock`, was used to protect the critical section as well as to support parallel page fault. When one process is blocked on I/O due to swapping to/from swap space, the blocked process would hold a `page_lock` that is associated with the evicting/reclaiming page. Therefore, other processes can still page fault without being blocked. The following describes the usage of each lock:
- The `page_lock` was needed for the case when another process would try to evict the page when the current process is freeing it or when the current process access the page while another process is evicting it. 
- The access to the same frame is protected by `frame_table_lock` and removed from the frame table to ensure other processes cannot access it.
- The `swap_lock` protect race condition in swap space. Although the I/O operations are sequential, the lock was required to ensure sequential updates on the bitmap that manage free/used space for swap space.