<link rel="stylesheet" href="styles.css">

# Dynamic memory allocation
#### Bug Lee (Chungeun Lee)
#### bug19@vt.edu
#### 2022

## Introduction
64-bit allocator based on segregated free lists, 
where lists are organized using explicit free lists approach. 

## Overview
Blocks are separated into two categories: allocated (used) and freed. 
Used blocks contain a header, payload, footer, and possibly some padding. 
On the other hand, freed blocks contain pointers for the previous and 
next freed blocks in the place of the payload.

The heap needs to be initialized through mm_init(). This set up the 
starting alignment for the heap, prologue/epilogue blocks, and segregated 
free lists with different bin sizes. It also expands the heap for the 
first time, marks the expanded block as a freed block, and added to the 
appropriate bin inside the segregated lists.  

When a client requests a certain size of bytes from the heap through 
mm_malloc(), the allocator first sees if there is a big enough freed 
block that can satisfy the request. This is done by selecting the correct 
bin (class) size among the segregated lists and finding the first 
available freed block. To reduce search time in an over-crowded bin, 
we have implemented find_fit() to only look for the first 10 blocks inside 
the bin. It then moves on to the next bin that contains a bigger freed 
block. Once the appropriate freed block has been found, the allocator 
marks the obtained block as used, remove the block from the free list bin,
and then returns the location of the block inside the heap to the client. 
If the size of the obtained block is big enough to leave room for minimum 
block size after splitting, the allocator split the block and add the split 
part back to the appropriate bin before returning. It was done so that a 
block would have less internal fragmentation. On the other hand, if no 
bins contain a big enough freed block for the requested size, the allocator 
extends the heap by a certain amount, then obtains the freed block from the 
extended part of the heap. Again, the block would get split and added to 
the appropriate bin if needed.  

A client can release the requested block back to the heap by calling 
mm_free(). The allocator first sees if the neighboring blocks are freed 
blocks and performs coalescing. After coalescing, then the allocator put 
the block to the appropriate bin of segregated lists.  

Finally, a client can send a request to re-sized the obtained block through 
mm_realloc(). In certain situations, mm_realloc() acts the same as mm_free 
(when a client request size 0) or mm_malloc (when a client request any 
size for the un-allocated block). If the requested size is smaller than 
the original block size, then the allocator performs no operation. For most 
other situations, the allocator would have to find a new location inside 
the heap, that is big enough to fit the requested size, and copy/move the 
data to the new location. However, that can be avoided in a few following 
cases. If the original block is at the end of the heap, then the allocator 
simply expands the heap and the original block gets resized at the same 
spot. If the block that is next to the original block is a freed block 
and the combined size is bigger than the requested size, then the two 
blocks are combined and the original block gets resized at the same spot.

## Base code
The source code was built upon mm-gback-implicit.c from Dr. back,
which was based on http://csapp.cs.cmu.edu/3e/ics3/code/vm/malloc/mm.c 

## Source code
Check out `src/lib/user/malloc.c`