<link rel="stylesheet" href="styles.css">

# Project 4: File Systems design documentation
#### CS 4284, Virginia Tech
#### Bug Lee (Chungeun Lee)
#### bug19@vt.edu
#### April 16, 2022

## Introduction
In Pintos project 4, support for file systems has been improved. First, the addition of the buffer cache reduced the number of I/O access to the disk. Secondly, the disk inode structure was reformatted to record all sectors that hold data of the file, enabling a file extension that is less prone to external fragmentation. Lastly, support for subdirectories and the concept of the path was added, increasing the capability of the Pintos file system closer to modern file systems.  
Note that our current implementation was based on Project 2 codebase. We have made this decision since Project 2 has been revised and reviewed multiple times by team members and received feedback from Dr. Back. We also believe that this made our problem space smaller and easier to perform regression tests. However, we plan to integrate Project 3, virtual memory, during the open-ended project.


# Buffer cache
The current buffer cache is implemented using a linked list where the size of the buffer cache is set to 64 cache blocks during initialization. The cached blocks are sorted by their access time, keeping the most recently used block on the front and the least recently used block on the back. This way, selecting an evicting block is simply picking the backmost block in the cache.

## Cache interface
In the base code, reading and writing from/to a file is done by `inode_read_at()` and `indoe_write_at()`. Inside thoes two functions, data are read/written directly from/to the disk using `block_read()` and `block_write()`. Our job was to reduce the number of I/O accesses (which is likely a performance bottleneck) by introducing a cache in the middle. Therefore, the following changes were made to the `inode_read_at()` and `inode_write_at()`.
- `inode_read_at()` and `inode_write_at()` now only interact with buffer cache. That is, accessing to disk is done by buffer cache under the hood.
- The bounce buffer has been removed. Now, partial read/write operation can be done directly to the cache block.

## Accessing/modifying cache block:
The following describes the steps to read/write cache block. In our current code base, this was divided into `cache_get_block()`, `cache_read_block()`, and `cache_put_block()`.
1. Lock the buffer cache.
2. Iterate over the buffer cache and check if the requested block was already cached into the buffer cache. If so, select the found cache block. If not found but the buffer cache is not full, then select one of the empty blocks inside the buffer cache. Otherwise, select the back most block in the buffer cache (least recently used block) that is unpinned (another process not holding the read-write lock). See the eviction section.
3. Move the selected block to the front of the buffer cache to indicate that it has been accessed recently.
4. Unlock the buffer cache. This way, other processes can access the buffer cache while the current process operates on the selected cache block.
5. Lock the selected cache block.
6. Check the sector of the selected block to see if it is still holding the requested block. This is needed since another process could have evicted the block in between steps 4 and 5. If the sector does not match, then unlock the cache block and go back to step 1.
7. If the block is valid (already cached), then access/update the cache block. If the block is invalid (empty or evicting block), first load the data into the cache block from the disk. Then, access/update the cache block.
8. Unlock the cache block.

## Eviction
To make LRU implementation simple, the cache was implemented with a list where the blocks are sorted by the accessed time. Therefore, selecting an LRU block was simply picking the backmost block in the buffer cache. The write-back policy was used: only writing back to disk when a block is dirty and gets evicted. The following describes the algorithm:
1. Pre-condition: The buffer cache is sorted in the recently accessed order. That is, the most recently accessed block should be in front, whereas the least recently used block should be on the back. 
2. Select the backmost block that is not pinned by another process.
3. If the block is not dirty, then do nothing. The selected block can be simply discarded.
4. Otherwise, write back the block data to the disk.

## Read-Write locks
The synchronization on the buffer cache was done using read-write locks. However, we have a situation where there are two groups that share one lock. To avoid monopolizing the lock to one group (and starving out the other), the ownership of the lock must take turns. That is, if a reader is holding a lock, then it should only accept other readers when no writers are pending (so that a waiting writer can go next). If a writer is holding a lock, then it must let all the pending readers go next.

## Read-ahead
The current approach is to spawn a new kernel thread during file system initialization which would be dedicated for read-ahead and nothing else. Running processes would then enqueue the next sector to be accessed to the queue, where the read-ahead thread would dequeue it and prefetch the sector to the cache while the requested process is still reading the previous sector.  
The new data structure `array_queue` was implemented to better synchronize the global read queue. The implementation is based on a ring buffer with a consumer/producer monitor. We provision that this can be reused for pipe implementation in an open-ended project with slight modification.

## Write-behind
The strategy is similar to the read-ahead, where one new kernel thread is dedicated to performing write-behind and nothing else. The write-behind is done by periodically scanning the buffer cache, currently every 5 seconds, and flushing any dirty blocks back to the disk. The timer_sleep() from project 1 was used to block the kernel thread when not in operation instead of busy waiting. This safety mechanism can protect file data against unexpected crashes.


# Extensible Files
In the base Pintos, the file size was fixed upon creation and the directory can only contain 16 files at most. Due to the layout of the underlying original inode structure, adding support for file growth was subject to external fragmentation. For example, if the next consecutive sector was unavailable, the OS needed to scan the disk space and then reallocate the file to the larger space that can fit. Therefore, to better utilize disk space and still support the allocation of consecutive sectors in the general case, the disk inode was reformatted to use multi-level indices.

## Multi-level indices
The idea is for the disk inode to keep track of each data sector instead of recording only the starting sector. However, due to limitations in disk inode size (512 bytes), the naive approach would create a table that can only hold 125 sector numbers (the other 3 are used for `length`, `magic`, and `is_dir`), limiting the maximum file size to 125 sectors (62.5 KB). To overcome this issue, two sectors were reserved for the indirect table and doubly indirect table.  The indirect table can hold additional 128 sectors that correspond to a data block, and the doubly indirect table can hold 128 sectors that correspond to the indirect table. With this new organization, the size of the file can reach up to 123 + 128 + 128^2 = 16635 sectors (8.12 MB).

## Sparse file implementation
After reformatting the disk inode structure to incorporate multi-indices, sparse file implementation was considered. We conclude that a sparse file is superior to a dense file as it uses disk space more efficiently by lazily loading the empty sectors instead of eagerly filling the space with 0s.  
- Most of the heavy lifting is done in `byte_to_sector()`, where it translates the byte position into the sector location on the disk. When encountering an empty entry on the mapping (either direct, indirect, or doubly indirect) table during a read, it would inform the caller that the space is empty (yet to be written). The read operation would then insert 0s into the buffer instead of reading the sector from the buffer cache. On a write, however, it assigns a new sector by scanning the free map for the unused sector on the disk, thereby lazily filling the space on file upon write request.
- In the current implementation of the sparse file, only one sector gets allocated during the file creation. The changes were made in `inode_create()` to write 1 byte to the end of the file, leaving other sectors empty (if the file size is >= 2 sectors). However, we later realize that allocation of the last sector is not necessary and plan to change it in the future project. 
- Lastly, the algorithm for deleting a file was changed in `inode_close()`. It now scans and deallocates the entire data blocks that correspond to the file from the disk. If the file is holding an indirect or doubly indirect table, all sectors that hold an indirect table gets deallocated from the disk as well.

## File extension
File growth can only happen during the write operation. Invoking `byte_to_sector()` during the write operation will expand the file. Once the write is done, the length of the file gets updated. This way, the readers would not see the expanded part of the file until the writing has finished.

## Synchronization
We successfully removed the global file lock from project 2 after the implementation of the extensible file was completed. Now, the synchronization on the file is handled by locking the requested block on the buffer cache. This way, multiple processes can access different parts of the file concurrently without blocking each other. When it comes to accessing/modifying the same block, there can be many readers but only one writer is allowed. As discussed in the file extension, the readers cannot read the part that is expanding until the expansion is done.  
In the base code, open inodes lists were unprotected. Locking is now done during the scan, add, and removal operation by using `inodes_lock`.


# Subdirectory
The base pintos only had support for the root directory. Support for subdirectory was added in this project, thereby enabling hierarchical directory structure. However, many features from the based Pintos file system were kept. For example, the file system still assumes that name of the file/directory is a maximum of 14 characters long. Also, the directory still organizes entries in a linear list of (inode, name) pairs. The main change in the file system for subdirectory support is the introduction of the path. 

## Path traversal
The idea is to traverse a given path until it reaches the destination (rightmost) directory. The given path was copied and then parsed by `strtok_r()` to avoid modification. The following algorithm was used:
1. Determine the starting directory by checking if the path includes `/` as a first character. If starts with `/`, then the traversal starts from the root directory. Otherwise, the current working directory (CWD) from the current process is used as a starting point. 
2. From the chosen directory, find a subdirectory that matches the name from the current position in the path. 
3. Once the next subdirectory is found, close the current directory in traversal and move on to the next one. Go back to step 2 and repeat until the rightmost directory is reached.

## Current working directory
`struct dir *current_dir` was added to `struct thread` so that each process keeps track of its current working directory (CWD). During process creation, the child inherits the CWD from its parent. However, once inherited, the parent and child CWD become independent. That is, changing or closing the CWD in the parent does not affect the child's CWD.  
A process may not have CWD (have CWD as `NULL`). This is the case for the initial thread as it has no parent or any child that inherits from the parent who has no CWD. In thoes cases, the root directory will be open as a CWD during the path traversal.

## Directory create/open/remove
The create/open/remove operations needed some adjustment to support directories as well as files. All three operations first start with locating the rightmost directory from the given path (see path traversal). The following describes the what happens in each operation once the process opens the rightmost directory:  
- **Creating a directory :**  
`filesys_dir_create()` was added to handle the creation of directory, separate from `filesys_create()`. The difference is that it invokes `dir_create()` (which calls `inode_create()` with `is_dir` flag to indicate directory), and adds "." and ".." directory entries to the newly created directory as a default. The root directory is an exception where its creation happen during `do_format()` and ".." point to root itself since root does not have a parent.  
- **Opening a directory :**  
Since the directory is also a file, `filesys_open()` was reused and most logic remained the same. However, when the path ends with `/`, it invokes the `file_open()` on the rightmost directory found from the path traversal. This handles the case when a user requests `open("/")`.  
- **Removing a directory :**  
Besides the addition of path traversal, no logic was changed in `filesys_remove()`. However in `dir_remove()`, a new policy was added to make sure only the empty directory can be removed. Like Unix, CWD or any open directory can be deleted, but disallow further usage by checking the `removed` flag in the in-memory inode.

## Subdirectory system calls and open directory table
The open directory table, `fd_to_dir`, was added to store a mapping between file descriptor and directory. Currently, it shares the index (file descriptor) with the open file table, `fd_to_file`, and acts as an indication to determine if a file type is a directory. The new syscalls `sys_inumber()`, `sys_isdir()`, and `sys_readdir()` use this feature to validate if the given file descriptor is directory.  
Two syscalls related to directory control, `sys_chdir()` and `sys_mkdir()` is a wrappers for path traversal and directory creation discussed above. On the other hand, `sys_readdir()` is a wrapper for `dir_readdir()` where it moves the position to the next directory entry each time it gets a call. `dir_readdir()` was adjusted to skip "." and ".." entries.

## Synchronization
An additional lock, `dir_lock`, was added to the in-memory inode to ensure a directory would contain file/directory names that are unique. Reading from and writing to directory disk inode (`dir_add()`, `dir_remove()`, and `dir_lookup()`) are atomic to each other using `dir_lock`.
