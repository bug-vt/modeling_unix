#include "vm/mem.h"
#include "userprog/pagedir.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "stdio.h"

static bool mem_install_page (void *upage, void *kpage);

/* Change the location of the program break, which defines the
   end of the process's data segment. Increasing the program
   break has the effect of allocating memory to the process. 
   sbrk increments the program's data space by INCREMENT bytes.
   Calling sbrk with an INCREMENT of 0 can be used to find the
   current location of the program break. */
uintptr_t 
mem_sbrk (intptr_t increment)
{
  /* Does not support heap shrinking. */
  if (increment < 0)
    return -1;

  struct thread *cur = thread_current ();
  uintptr_t prev_end = cur->heap_end;
  int size = increment;

  void *current_heap = pg_round_down ((void *) (cur->heap_end + increment));
  /* Check if increment would go outside the current heap page.
     In that case, allocate additional memory from kernel. */
  if (increment > PGSIZE || !pagedir_get_page (cur->pagedir, current_heap))
    {
      /* Find the next page boundary. */
      void *upage = pg_round_up ((void *) cur->heap_end);
      size -= ((uintptr_t) upage - cur->heap_end);

      while (size > 0)
        {
          /* Get a page of memory. */
          uint8_t *kpage = palloc_get_page (PAL_USER | PAL_ZERO);
          if (kpage == NULL)
            return -1;

          /* Add the page to the process's address space. */
          if (!mem_install_page (upage, kpage))
            {
              palloc_free_page (kpage);
              return -1;
            }
          
          /* Advance. */
          size -= PGSIZE;
          upage += PGSIZE;
        }
    }
 
  cur->heap_end += increment;

  return prev_end;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   The user process may modify the page.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
mem_install_page (void *upage, void *kpage)
{
  struct thread *t = thread_current ();

  if (pagedir_get_page (t->pagedir, upage) != NULL)
    printf ("-------***********%p\n", upage);

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, true));
}
