#include "userprog/process.h"
#include "userprog/syscall.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "threads/gdt.h"
#include "userprog/pagedir.h"
#include "threads/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include <threads/malloc.h>
#include <lib/stdio.h>
#include <threads/synch.h>
#include <threads/spinlock.h>
#include <userprog/syscall.h>

static bool load (const char *cmdline, void (**eip) (void), void **esp);
/* Our Code */
static thread_func dup_process NO_RETURN;
static int setup_args (const char *str, void **esp);
static struct maternal_bond * find_child (tid_t child_tid);
static bool is_orphan_or_zombie (struct maternal_bond *bond);


/* Hold data that is needed for thread duplication. */
struct parent_copy
  {
    uint32_t *pagedir;
    struct intr_frame *if_;
  };

/* Starts a new thread that is exact same copy of calling (parent)
   thread. The new thread may be scheduled (and may even exit)
   before process_fork() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_fork (struct intr_frame *if_)
{
  tid_t tid;   
  struct thread *parent = thread_current ();

  struct parent_copy *copy = malloc (sizeof (struct parent_copy));
  copy->pagedir = parent->pagedir;
  copy->if_ = if_;

  /* Create a new thread that is duplicate of parent. */  
  tid = thread_create (parent->name, NICE_DEFAULT, dup_process, copy); 
  if (tid == TID_ERROR)
    goto fork_err;

  /* Wait for newly created process to finish initialize. */
  struct maternal_bond *bond = find_child (tid);
  sema_down (&bond->load);
  /* Fail during duplication. */
  if (bond->load_fail)
    {
      /* Wait for the child process to completely exit. */
      sema_down (&bond->exit);
      list_remove (&bond->elem);
      free (bond);
      tid = TID_ERROR;
    }

  return tid;

fork_err:
  return TID_ERROR;
}

/* A thread function that copies a parent process and starts
   it running. */
static void
dup_process (void *copy_)
{
  struct parent_copy *copy = (struct parent_copy *) copy_;
  struct thread *t = thread_current ();
  bool success = false;

  /* Copy parent's interrupt frame. */
  struct intr_frame if_;
  memcpy (&if_, copy->if_, sizeof (struct intr_frame));
  /* Set return value to 0. */
  if_.eax = 0;

  /* Copy page directory from parent and activate page directory. */
  t->pagedir = pagedir_copy (copy->pagedir);
  if (t->pagedir == NULL) 
    goto dup_done;
  process_activate ();

  /* At this point, duplication have succeed. */
  free (copy);
  success = true;
  t->bond->load_fail = false;

dup_done:
  /* We arrive here whether the load is successful or not. */
  /* Notify the parent that load was completed (or failed). */
  sema_up (&t->bond->load);

  /* If duplication failed, quit. */
  if (!success)
    sys_exit (-1);
  
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t 
process_execute (const char *cmd_line) 
{   
  char *line_copy;   
  tid_t tid;   

  /* Make a copy of FILE_NAME.      
     Otherwise there's a race between the caller and load(). 
     Also note the following: 
     (1) Command line passed to the Pintos kernel itself is
         limited to 128 bytes.
     (2) If called by exec, str_copy_from_user() make sure cmd_line is 
         less than or equal to one page. */
  line_copy = palloc_get_page (0);   
  if (line_copy == NULL)     
    return TID_ERROR;   
  strlcpy (line_copy, cmd_line, PGSIZE);   

  /* Extract executable file name from raw input line. */
  char *fname, *save_ptr;   
  fname = strtok_r ((char *) cmd_line, " ", &save_ptr);

  /* Create a new thread to execute executable file. */  
  tid = thread_create (fname, NICE_DEFAULT, process_start, line_copy); 
  if (tid == TID_ERROR)
    {
      palloc_free_page (line_copy);
      goto exec_done;
    }

  /* Wait for newly created process to finish loading. */
  struct maternal_bond *bond = find_child (tid);
  sema_down (&bond->load);
  /* Load error */
  if (bond->load_fail)
    {
      /* Wait for the child process to completely exit. */
      sema_down (&bond->exit);
      list_remove (&bond->elem);
      free (bond);
      tid = TID_ERROR;
    }

exec_done:
  return tid; 
} 

/* A thread function that loads a user process and starts it
   running. */
void
process_start (void *file_name_)
{
  struct thread *cur = thread_current ();
  char *cmd_line = file_name_;
  struct intr_frame if_;
  bool success;

  char *line_copy = palloc_get_page (0);   
  if (line_copy == NULL)     
    sys_exit (-1);
  strlcpy (line_copy, cmd_line, PGSIZE);   

  /* Extract executable file name from raw input line. */
  char *exec_name, *save_ptr;   
  exec_name = strtok_r ((char *) line_copy, " ", &save_ptr);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  uint32_t *pd;
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  success = load (exec_name, &if_.eip, &if_.esp);
  strlcpy (cur->name, exec_name, sizeof cur->name);

  /* Set up user provided arguments to the stack. */
  int args_val = setup_args (cmd_line, &if_.esp);

  /* If called by exec, cmd_line is a copy from parent. 
     Setting syscall_arg of child to NULL have no effect on parent.
     Otherwise, If called by exec2, cmd_line is syscall_arg from 
     calling process. Once cmd_line is freed, syscall_arg must be NULL. */
  palloc_free_page (line_copy);
  palloc_free_page (cmd_line);
  cur->syscall_arg = NULL;
  /* If load failed, quit. */
  if (!success || args_val == -1)
    sys_exit (-1);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting. */
int
process_wait (tid_t child_tid) 
{
  struct maternal_bond *bond = find_child (child_tid);
  /* The parent does not have a child with given tid. */
  if (!bond)
    return -1;

  /* Wait for the child to exit. */
  sema_down (&bond->exit);
  /* Remove the child from the children list. */ 
  list_remove (&bond->elem);
  /* At this point, child have exited and must be a zombie.
     Reap the child. */
  bool reap = is_orphan_or_zombie (bond);
  int status = bond->status;

  if (reap)
    free (bond);
  
  return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }

  /* Free copy of the user provided argument. */
  if (cur->syscall_arg != NULL)
    palloc_free_page (cur->syscall_arg);

  /* Print termination message. */
  int status = cur->bond->status;
  printf("%s: exit(%d)\n", cur->name, status);

  /* Closes all open file descriptors and free the fd table. */
  struct file **fd_table = cur->fd_table;
  for (int fd = 0; fd < FD_MAX; fd++)
    {
      if (fd_table[fd] != NULL)
        {
          dir_close (file_get_directory (fd_table[fd]));
          file_close (fd_table[fd]);
        }
    }
  palloc_free_page (cur->fd_table);

  /* Close the executable that was running on exiting process.
     This re-enable the write permission. */
  file_close (cur->exec_file);
  /* Close the current working directory */
  dir_close (cur->current_dir);

  /* Remove children list and reap zombie children if any. 
     Otherwise, make the alive children to orphan. */
  for (struct list_elem *e = list_begin (&cur->children);
       e != list_end (&cur->children);)
    {
      struct maternal_bond *bond = list_entry (e, struct maternal_bond, elem);

      bool reap = is_orphan_or_zombie (bond);
      e = list_remove (e);
      if (reap)
        free (bond);
    }

  /* Break the bond between the current process and its parent
     by decrementing the reference counter. 
     If the current process is orphan, then reap itself.
     Otherwise, become a zombie */
  bool reap = is_orphan_or_zombie (cur->bond);

  /* Notify the parent of the current process that it has been exited. */
  sema_up (&cur->bond->exit);
  if (reap)
    free (cur->bond);
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
static bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Disable write permission to the currently running executable. 
     This will be re-enable once process exit. */
  file_deny_write (file);

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  uintptr_t heap_start = 0;
  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
              
              uint32_t segment_end = mem_page + read_bytes + zero_bytes;
              if (segment_end > heap_start)
                heap_start = segment_end;
            }
          else
            goto done;
          break;
        }
    }

  /* Set the start of the heap right after the end of BSS. */
  heap_start += PGSIZE;
  t->heap_end = heap_start;

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;
  t->bond->load_fail = false;

 done:
  /* We arrive here whether the load is successful or not. */
  /* Record executable file that was loaded to the process */
  file_close (t->exec_file);
  t->exec_file = file;
  /* Notify the parent that load was completed (or failed). */
  sema_up (&t->bond->load);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }

  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else
        palloc_free_page (kpage);
    }
  return success;
}


/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

/* Sets up the stack so that it can be used in
 * user programs */
static int 
setup_args(const char *cmd_line, void **esp)
{
  /* Note that the size of cmd_line is:
     (1) <= 128 bytes if cmd_line was passed to the Pintos kernel itself.
     (2) <= 4096 bytes if passed by exec. This is because str_copy_from_user() 
         only copy up to 4096 bytes.  
         
     However, including meta data (argc, argv[0]...argv[n], **argv) can 
     still overflow the initial stack. */
  size_t size = strlen(cmd_line) + 1; 

  char *line_copy = malloc (size);
  if (line_copy == NULL)     
    return -1;   
  strlcpy (line_copy, cmd_line, size);   
  char **argv = calloc (size, 1);

  if (argv == NULL)
    {
      free (line_copy);
      return -1;
    }

  char *token, *save_ptr;
  int pos = 0;   
  for (token = strtok_r ((char *) line_copy, " ", &save_ptr); token != NULL; 
       token = strtok_r (NULL, " ", &save_ptr))     
    {
      argv[pos++] = token;     
    }

   /* Check for stack overflow. 
      - size = total string size of arguments. 
      - size % 4 = word-align padding.
      - ((pos + 1) * 4) = argv[0] ... argv[n], including null sentinel. 
      - (4 * 3) = argv, argc, and ret */
  if ((size + (size % 4)) + ((pos + 1) * 4) + (4 * 3) > PGSIZE) 
    {
      free (line_copy);
      free (argv);
      return -1;
    }

  uint32_t pointers[pos];
  int offset = 0;
  /* Adding the strings to the stack */
  for (int index = pos - 1; index >= 0; index --)
    {
      int len = strlen(argv[index]) + 1;
      char *str_temp = (char*)*esp;
      str_temp -= len;
      *esp = *esp - len;
      pointers[index] = (uint32_t)str_temp;
      strlcpy(str_temp, argv[index], len);
      offset += len;
    }
  /* Adding the word-align */
  *esp -= 4 - (offset % 4);
  memset(*esp, 0, 4 - (offset % 4));
  offset += 4 - (offset % 4);
  /* Adding the pointers */
  char **str_ptr = (char**)*esp;
  *esp -= 4;
  offset += 4;
  *(-- str_ptr) = NULL;
  for (int index = pos - 1; index >= 0; index --)
    {
      *esp -= 4;
      offset += 4;
      *(-- str_ptr) = (char *)pointers[index];
    }
  /* Adding the pointer to the char* */
  uint32_t *ptr = (uint32_t *)*esp;

  *esp -= 4;
  offset += 4;
  ptr --;
  *ptr = (uint32_t)(ptr + 1);

  /* Adding the number of args */
  *esp -= 4;
  offset += 4;
  ptr --;
  *ptr = pos;

  /* Going back to the return arg */
  ptr --;
  *esp -= 4;
  offset += 4;

  free (line_copy);
  free (argv);
  return offset;
}

/* Find the share data (bond) between the current process
   and one of its child from the children list using the given tid. */
static struct maternal_bond *
find_child (tid_t child_tid)
{
  struct thread *curr = thread_current ();
  for (struct list_elem *e = list_begin (&curr->children);
       e != list_end (&curr->children); e = list_next (e))
    {
      struct maternal_bond *bond = list_entry (e, struct maternal_bond, elem);
      if (bond->tid == child_tid)
        return bond;
    }

  return NULL;
}

/* Determine if the share data (bond) should be reaped. */
static bool
is_orphan_or_zombie (struct maternal_bond *bond)
{
  bool reap = false;
  spinlock_acquire (&bond->lock);
  bond->reference_counter--;
  if (bond->reference_counter == 0)
    reap = true;
  spinlock_release (&bond->lock);

  return reap;
}
