#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <debug.h>
#include <syscall.h>

#include "lib/kernel/list.h"

#define ALIGNMENT 16

struct boundary_tag {
    int inuse:1;        // inuse bit
    int pinuse:1;       // previous inuse bit
    int size:30;        // size of block, in words
                        // block size
};

/* FENCE is used for heap prologue/epilogue. */
const struct boundary_tag FENCE = {
    .inuse = 1,
    .pinuse = 0,
    .size = 0
};

/* A C struct describing the beginning of each block. 
 * For implicit lists, used and free blocks have the same 
 * structure, so one struct will suffice for this example.
 *
 * If each block is aligned at 12 mod 16, each payload will
 * be aligned at 0 mod 16.
 */
struct __attribute__((__packed__)) block {
    struct boundary_tag header; /* offset 0, at address 12 mod 16 */
    char payload[0];            /* offset 4, at address 0 mod 16 */
    struct list_elem elem;      /* offset 4 */
};

/* Basic constants and macros */
#define WSIZE       sizeof(struct boundary_tag)  /* Word and header/footer size (bytes) */
#define MIN_BLOCK_SIZE_WORDS  8 /* Minimum block size in words */ // size 8 works iff chunk size is 1<<7
#define CHUNKSIZE  (1<<7)  /* Extend heap by this amount (words) */

static inline size_t max(size_t x, size_t y) {
    return x > y ? x : y;
}

static size_t align(size_t size) {
  return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static bool is_aligned(size_t size) __attribute__((__unused__));
static bool is_aligned(size_t size) {
  return size % ALIGNMENT == 0;
}

/* Global variables */
static struct block *heap_listp = 0;    /* Pointer to first block */
#define NUM_LISTS 16   /* The size of the list array */
static struct list seg_list[NUM_LISTS]; /* A list array that simulates segregated free list */
static size_t sizes[16] = {
    8,      /* 0 */
    24,     /* 1 */
    48,     /* 2 */
    80,     /* 3 */
    160,    /* 4 */
    320,    /* 5 */
    640,    /* 6 */
    960,    /* 7 */
    1024,   /* 8 */
    2048,   /* 9 */
    3072,   /* 10 */
    4096,   /* 11 */
    6144,   /* 12 */
    8192,   /* 13 */
    16384,  /* 14 */
    32768   /* 15 */
}; 

/* Function prototypes for internal helper routines */
static int mm_init (void);
static struct block *extend_heap(size_t words, bool coal);
static void place(struct block *bp, size_t asize);
static struct block *find_fit(size_t asize);
static struct block *coalesce(struct block *bp);
static struct list *get_list(size_t size);
static int get_non_empty_list(size_t size);


/* Given a block, obtain previous's block footer.
   Works for left-most block also. */
static struct boundary_tag * prev_blk_footer(struct block *blk) {
    return &blk->header - 1;
}

/* Return if block is free */
static bool blk_free(struct block *blk) { 
    return !blk->header.inuse; 
}

/* Return size of block is free */
static size_t blk_size(struct block *blk) { 
    return blk->header.size; 
}

/* Given a block, obtain pointer to previous block.
   Not meaningful for left-most block. */
static struct block *prev_blk(struct block *blk) {
    struct boundary_tag *prevfooter = prev_blk_footer(blk);
    ASSERT(prevfooter->size != 0);
    return (struct block *)((void *)blk - WSIZE * prevfooter->size);
}

/* Given a block, obtain pointer to next block.
   Not meaningful for right-most block. */
static struct block *next_blk(struct block *blk) {
    ASSERT(blk_size(blk) != 0);
    return (struct block *)((void *)blk + WSIZE * blk->header.size);
}

/* Given a block, obtain its footer boundary tag */
static struct boundary_tag * get_footer(struct block *blk) {
    return ((void *)blk + WSIZE * blk->header.size)
                   - sizeof(struct boundary_tag);
}

/* Set a block's size and inuse bit in header and footer */
static void set_header_and_footer(struct block *blk, int size, int inuse) {
    blk->header.inuse = inuse;
    blk->header.size = size;
    * get_footer(blk) = blk->header;    /* Copy header to footer */
}

/* Mark a block as used and set its size. */
static void mark_block_used(struct block *blk, int size) {
    //set_header_and_footer(blk, size, 1);
    blk->header.inuse = 1;
    blk->header.size = size;
    next_blk(blk)->header.pinuse = 1;
}

/* Mark a block as free and set its size. */
static void mark_block_free(struct block *blk, int size) {
    set_header_and_footer(blk, size, 0);
    next_blk(blk)->header.pinuse = 0;
}

/* 
 * mm_init - Initialize the memory manager 
 */
static int mm_init(void) 
{
    ASSERT (offsetof(struct block, payload) == 4);
    ASSERT (sizeof(struct boundary_tag) == 4);
    /* Create the initial empty heap */
    struct boundary_tag * initial = sbrk(4 * sizeof(struct boundary_tag));
    if (initial == (void *)-1)
        return -1;

    /* We use a slightly different strategy than suggested in the book.
     * Rather than placing a min-sized prologue block at the beginning
     * of the heap, we simply place two fences.
     * The consequence is that coalesce() must call prev_blk_footer()
     * and not prev_blk() because prev_blk() cannot be called on the
     * left-most block.
     */
    initial[2] = FENCE;                     /* Prologue footer */
    heap_listp = (struct block *)&initial[3];
    initial[3] = FENCE;                     /* Epilogue header */
    initial[3].pinuse = 1;
    for (int x = 0; x < NUM_LISTS; x++) {
        list_init(&seg_list[x]);
    }

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE, true) == NULL) 
        return -1;

    return 0;
}

/* 
 * malloc - Allocate a block with at least size bytes of payload 
 */
void *malloc(size_t size)
{
    if (heap_listp == 0)
      mm_init();

    struct block *bp;      

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;
    /* Adjust block size to include overhead and alignment reqs. */
    size += 1 * sizeof(struct boundary_tag);    /* account for tags */
    /* Adjusted block size in words */
    size_t awords = max(MIN_BLOCK_SIZE_WORDS, align(size)/WSIZE); /* respect minimum size */
    /* Search the free list for a fit */
    if ((bp = find_fit(awords)) != NULL) {
        list_remove(&bp->elem);
        place(bp, awords);
        return bp->payload;
    }

    /* No fit found. Get more memory and place the block */
    size_t extendwords = max(awords,CHUNKSIZE); /* Amount to extend heap if no fit */
    if ((bp = extend_heap(extendwords, true)) == NULL)  
        return NULL;
    list_remove(&bp->elem);
    place(bp, awords);
    return bp->payload;
} 

/* 
 * free - Free a block 
 */
void free(void *bp)
{
    ASSERT (heap_listp != 0);       // assert that mm_init was called
    if (bp == 0) 
        return;

    /* Find block from user pointer */
    struct block *blk = bp - offsetof(struct block, payload);
    struct list * temp = get_list(blk->header.size);
    list_push_back(temp, &blk->elem);

    mark_block_free(blk, blk_size(blk));
    coalesce(blk);
}

/*
 * coalesce - Boundary tag coalescing. Return ptr to coalesced block
 */
static struct block *coalesce(struct block *bp) 
{
    //bool prev_alloc = prev_blk_footer(bp)->inuse;   /* is previous block allocated? */
    bool prev_alloc = bp->header.pinuse;
    bool next_alloc = ! blk_free(next_blk(bp));     /* is next block allocated? */
    size_t size = blk_size(bp);

    if (prev_alloc && next_alloc) {            /* Case 1 */
        // both are allocated, nothing to coalesce
        return bp;
    }

    else if (prev_alloc && !next_alloc) {      /* Case 2 */
        // combine this block and next block by extending it
        struct block * bp_next = next_blk(bp);
        mark_block_free(bp, size + blk_size(bp_next));
        list_remove(&bp_next->elem);
        list_remove(&bp->elem);
        struct list * temp = get_list(bp->header.size);
        list_push_back(temp, &bp->elem);
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3 */
        // combine previous and this block by extending previous
        struct block * bp_next = bp;
        bp = prev_blk(bp);
        mark_block_free(bp, size + blk_size(bp));
        list_remove(&bp_next->elem);
        list_remove(&bp->elem);
        struct list * temp = get_list(bp->header.size);
        list_push_back(temp, &bp->elem);
    }

    else {                                     /* Case 4 */
        // combine all previous, this, and next block into one
        struct block * bp_next_next = next_blk(bp);
        struct block * bp_next = bp;
        mark_block_free(prev_blk(bp), 
                        size + blk_size(next_blk(bp)) + blk_size(prev_blk(bp)));
        bp = prev_blk(bp);
        list_remove(&bp_next->elem);
        list_remove(&bp_next_next->elem);
        list_remove(&bp->elem);
        struct list * temp = get_list(bp->header.size);
        list_push_back(temp, &bp->elem);
    }
    return bp;
}

/*
 * realloc - Naive implementation of realloc
 */
void *realloc(void *ptr, size_t size)
{
    if (heap_listp == 0)
      mm_init();

    /* If size == 0 then this is just free, and we return NULL. */
    if (size == 0) {
        free(ptr);
        return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL) {
        return malloc(size);
    }

    struct block *oldblock = ptr - offsetof(struct block, payload);
    size_t oldsize = blk_size(oldblock) * WSIZE;

    //Adjusted block size in words
    size_t use_size = align(size+2*sizeof(struct boundary_tag)) / WSIZE; 
    // realloc case 0

    if (use_size <= oldsize) {
        return ptr;
    }

    struct block * bp_next = next_blk(oldblock);
    size_t newsize = 0;

    // realloc case 5

    if (bp_next->header.inuse == -1 && bp_next->header.size == 0) {
        size_t n_size = use_size - oldsize;
        if (n_size > 0 && use_size != oldsize) {
            size_t awords = max(MIN_BLOCK_SIZE_WORDS, align(n_size)/WSIZE);
            struct block * heap_bl = extend_heap(awords, true);
            list_remove(&heap_bl->elem);
            oldblock->header.size += awords;
            place(oldblock, oldblock->header.size);
            return oldblock->payload;
        }
    }

    // realloc case 2

    newsize = blk_size(oldblock) + blk_size(bp_next);
    newsize *= WSIZE;

    if (blk_free(bp_next) && newsize >= use_size) {
        /* Adjust block size to include overhead and alignment reqs. */
        size += 1 * sizeof(struct boundary_tag);    /* account for tags */
        /* Adjusted block size in words */
        size_t awords = max(MIN_BLOCK_SIZE_WORDS, align(size)/WSIZE); /* respect minimum size */
        mark_block_used(oldblock, newsize / WSIZE);
        list_remove(&bp_next->elem);
        place(oldblock, awords);
        return oldblock->payload;

    }  

    // realloc case 1 & 4
    
    if (!oldblock->header.pinuse) {
        struct boundary_tag * bt_prev = prev_blk_footer(oldblock);
        newsize = blk_size(oldblock) + bt_prev->size + blk_size(bp_next);
        newsize *= WSIZE;
        if (blk_free(bp_next) && newsize >= use_size) {
            /* Adjust block size to include overhead and alignment reqs. */
            size += 1 * sizeof(struct boundary_tag);    /* account for tags */
            /* Adjusted block size in words */
            size_t awords = max(MIN_BLOCK_SIZE_WORDS, align(size)/WSIZE); /* respect minimum size */
            struct block * bp_prev = prev_blk(oldblock);
            list_remove(&bp_prev->elem);
            list_remove(&bp_next->elem);
            mark_block_used(bp_prev, newsize / WSIZE);
            memmove(bp_prev->payload, oldblock->payload, awords * WSIZE);
            place(bp_prev, newsize / WSIZE);
            return bp_prev->payload;
        }
        newsize = blk_size(oldblock) + bt_prev->size;
        newsize *= WSIZE;
        if (newsize >= use_size) {
            /* Adjust block size to include overhead and alignment reqs. */
            size += 1 * sizeof(struct boundary_tag);    /* account for tags */
            /* Adjusted block size in words */
            size_t awords = max(MIN_BLOCK_SIZE_WORDS, align(size)/WSIZE); /* respect minimum size */
            struct block * bp_prev = prev_blk(oldblock);
            list_remove(&bp_prev->elem);
            mark_block_used(bp_prev, newsize / WSIZE);
            memmove(bp_prev->payload, oldblock->payload, awords * WSIZE);
            place(bp_prev, newsize / WSIZE);
            if (blk_free(bp_next)) {
                coalesce(bp_next);
            }
            return bp_prev->payload;
        }
    }  

    void *newptr = malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if (!newptr) {
        return 0;
    }

    /* Copy the old data. */
    if (size < oldsize) oldsize = size;
    memcpy(newptr, ptr, oldsize);

    /* Free the old block. */
    free(ptr);
    return newptr;
}

/* 
 * The remaining routines are internal helper routines 
 */

/* 
 * extend_heap - Extend heap with free block and return its block pointer
 */
static struct block *extend_heap(size_t words, bool coal) 
{
    void *bp = sbrk(words * WSIZE);

    if ((intptr_t) bp == -1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header.
     * Note that we overwrite the previous epilogue here. */
    struct block * blk = bp - sizeof(FENCE);
    mark_block_free(blk, words);
    next_blk(blk)->header = FENCE;

    struct list * temp = get_list(blk->header.size);
    list_push_back(temp, &blk->elem);
    if (coal) {
        /* Coalesce if the previous block was free */
        return coalesce(blk);
    }
    else {
        return blk;
    }
}

/* 
 * place - Place block of asize words at start of free block bp 
 *         and split if remainder would be at least minimum block size
 */
static void place(struct block *bp, size_t asize)
{
    size_t csize = blk_size(bp);

    if ((csize - asize) >= MIN_BLOCK_SIZE_WORDS) { 
        mark_block_used(bp, asize);
        struct block * bp_next = next_blk(bp);
        mark_block_free(bp_next, csize-asize);
        struct list * temp = get_list(bp_next->header.size);
        list_push_back(temp, &bp_next->elem);
        coalesce(bp_next);
    }
    else { 
        mark_block_used(bp, csize);
    }
}

/* 
 * find_fit - Find a fit for a block with asize words 
 */
static struct block *find_fit(size_t asize)
{
    /* First fit search */
    int check = 0;
    int index = get_non_empty_list(asize);
    for (; index < NUM_LISTS; index++) {
        if (!list_empty(&seg_list[index])) {
            for (struct list_elem * e = list_begin (&seg_list[index]); e != list_end (&seg_list[index]); e = list_next(e)) {
                struct block * bp = list_entry(e, struct block, elem);
                if (asize <= blk_size(bp)) {
                    return bp;
                }
                check++;
                if (check >= 50) {
                    break;
                }
            }
        }
    }
    return NULL; /* No fit */
}

/* 
 * get_list - Given a size_t size, this function will return
 * a pointer to an element in a list array that simulate segregated
 * free lists.
 */
static struct list *get_list(size_t size) {
    if (size >= 32768) {
        return &seg_list[15];
    }
    else if (size >= 16384) {
        return &seg_list[14];
    }
    else if (size >= 8192) {
        return &seg_list[13];
    }
    else if (size >= 4096) {
        return &seg_list[12];
    }
    else if (size >= 2048) {
        return &seg_list[11];
    }
    else if (size >= 1024) {
        return &seg_list[10];
    }
    else if (size >= 512) {
        return &seg_list[9];
    }
    else if (size >= 256) {
        return &seg_list[8];
    }
    else if (size > 128) {
        return &seg_list[7];
    }
    else if (size >= 64) {
        return &seg_list[6];
    }
    else if (size >= 32) {
        return &seg_list[5];
    }
    else if (size >= 16) {
        return &seg_list[4];
    }
    else if (size >= 8) {
        return &seg_list[3];
    }
    else if (size >= 4) {
        return &seg_list[2];
    }
    else if (size >= 2) {
        return &seg_list[1];
    }
    return &seg_list[0];
}

/* 
 * get_non_empty_list - Given a size_t size, returns the a matching
 * array index that contains the size and is non-empty. Used in
 * find_fit to speed up searching.
 */
static int get_non_empty_list(size_t size) {
    if (size <= sizes[0] && !list_empty(&seg_list[0])) {
        return 0;
    }
    else if (size <= sizes[1] && !list_empty(&seg_list[1])) {
        return 1;
    }
    else if (size <= sizes[2] && !list_empty(&seg_list[2])) {
        return 2;
    }
    else if (size <= sizes[3] && !list_empty(&seg_list[3])) {
        return 3;
    }
    else if (size <= sizes[4] && !list_empty(&seg_list[4])) {
        return 4;
    }
    else if (size <= sizes[5] && !list_empty(&seg_list[5])) {
        return 5;
    }
    else if (size <= sizes[6] && !list_empty(&seg_list[6])) {
        return 6;
    }
    else if (size <= sizes[7] && !list_empty(&seg_list[7])) {
        return 7;
    }
    else if (size <= 512 && !list_empty(&seg_list[8])) {
        return 8;
    }
    else if (size <= 1024 && !list_empty(&seg_list[9])) {
        return 9;
    }
    else if (size <= 2048 && !list_empty(&seg_list[10])) {
        return 10;
    }
    else if (size <= 4096 && !list_empty(&seg_list[11])) {
        return 11;
    }
    else if (size <= 8192 && !list_empty(&seg_list[12])) {
        return 12;
    }
    else if (size <= 16384 && !list_empty(&seg_list[13])) {
        return 13;
    }
    else if (size <= 32768 && !list_empty(&seg_list[14])) {
        return 14;
    }
    else {
        return 15;
    }
}
