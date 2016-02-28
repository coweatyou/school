#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hash.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#endif

static thread_func execute_thread NO_RETURN;
static void *parse_args(char **, void *);
static bool load (const char *cmdline, void (**eip) (void), void **esp);

static hash_hash_func fd_hash_func;
static hash_less_func fd_less_func;
static hash_action_func close_fd_hash;
static inline struct fd_elem *fd_elem_from_hash(const struct hash_elem *e);

static hash_action_func wait_hash;
static hash_action_func donate_child;

static struct hash *inits_children;

/* Initializes parts of t that are owned by process.c */
void process_init (struct thread *t) {
  /* Malloc space for child's exit controler */
  struct exit_struct *ec = malloc(sizeof(struct exit_struct));

  /* Set up exit controler */
  sema_init(&ec->parent, 0);
  sema_init(&ec->running, 0);
  ec->tid = t->tid;
  ec->value = -1;
  ec->loaded = false;

  /* Add new exit_controler to this thread's children list. */
  hash_insert(&thread_current()->children, &ec->elem);
  if(thread_current()->exit_controler == NULL) {
    inits_children = &thread_current()->children;
  }

  /* Set new exit_controler as child's, init child's children and fdt list */
  t->exit_controler = ec;
  hash_init(&t->children, tid_hash, tid_less, NULL);
  hash_init(&t->fd_table, fd_hash_func, fd_less_func, NULL);
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  char *fn_copy, *t;//TODO t does nothing
  char thread_name[16];
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Get thread name */
  strlcpy (thread_name, file_name, 15);
  strtok_r(thread_name, " ", &t);//TODO this part needs lots of cleanup

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (thread_name, PRI_DEFAULT, execute_thread, fn_copy);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
execute_thread (void *file_name_)
{
  char *position, *s, *file_name;
  struct intr_frame if_;
  bool success;

  page_init();

  /* Initialize s to file name */
  file_name = strtok_r(file_name_, " ", &position);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  lock_acquire(&fs_lock);
  success = load (file_name, &if_.eip, &if_.esp);//TODO most of the modified functions here won't ever return false
  lock_release(&fs_lock);

  if(success) {
    /* Create arg list at bottom of temp page pg. */
    void *pg = palloc_get_page(0);
    if(pg != NULL) {
      char **tmp = pg;

      for(s = file_name; s != NULL; s = strtok_r(NULL, " ", &position)) {
        if_.esp -= strlen(s) + 1;
        strlcpy(if_.esp, s, strlen(s) + 1);
        *tmp++ = if_.esp;
      }

      if_.esp = parse_args(tmp, if_.esp);
      palloc_free_page(pg);
    } else {
      success = false;
    }
  }

  /* Tell your parent if you successfully loaded. */
  thread_current()->exit_controler->loaded = success;
  sema_up(&thread_current()->exit_controler->running);

  /* If load failed, quit. */
  palloc_free_page (file_name_);
  if (!success) {
    thread_exit ();
  }

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
  int val;
  struct hash_elem *e;
  struct exit_struct es;

  /* Delete child's tid from list. */
  es.tid = child_tid;
  e = hash_delete(&thread_current()->children, &es.elem);

  /* If child_tid was valid, wait for child. */
  if (e != NULL)
    wait_hash(e, &val);
  else
    val = -1;

  return val;
}

/* Waits on, then frees the resources of, the child_struct containing hash_elem
   e.  Aux must either be and pointer to an int where the child's exit value
   is to be stored or NULL. */
static void wait_hash(struct hash_elem *e, void *aux) {
  struct exit_struct *es;

  ASSERT(e != NULL);
  es = hash_entry(e, struct exit_struct, elem);

  /* Wait for child to exit. */
  sema_down(&es->parent);

  /* Get exit value and free the exit_struct's resources. */
  if(aux != NULL)
    *(int *) aux = es->value;
  free(es);
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

//  if(cur->exit_controler->value == -1)
//	  debug_backtrace();

  /* Re-enable writing to executable.  TODO problem with an already acquired lock is cause by
   * invalid writes to RO locations (needed to pass pt-code-write-2 test). */
  if(!lock_held_by_current_thread(&fs_lock))
  	lock_acquire(&fs_lock);
  if(cur->executable != NULL)
    file_close(cur->executable);
  lock_release(&fs_lock);

  /* Close any open files. */
  hash_destroy(&cur->fd_table, close_fd_hash);

  /* If you are init, wait for all children to exit. */
  if(cur->exit_controler == NULL)
    hash_apply(&cur->children, wait_hash);

  /* Donate any remaining children to init. */
  hash_destroy(&cur->children, donate_child);

  /* Free all pages. */
  page_destroy_all();

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Print exit message */
      printf("%s: exit(%d)\n", cur->name, cur->exit_controler->value);

      /* Release waiting parent */
      sema_up(&cur->exit_controler->parent);

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
}

/* Donates all living children to the init thread.  Called from process_exit. */
void donate_child (struct hash_elem *e, void *aux UNUSED) {
  struct exit_struct *es = hash_entry(e, struct exit_struct, elem);

  /* If child has already exited, you don't need to give it to init. */
  if(sema_try_down(&es->parent)) {
    free(es);
  } else {
    hash_insert(inits_children, e);
  }
}

/* Sets up the CPU for running user code in the current
   thread. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_set_esp0 ((uint8_t *) t + PGSIZE);
}

void *parse_args (char **plist_end, void *esp) {
  char **tmp;
  ptrdiff_t len = (void *) plist_end - pg_round_down(plist_end);

  /* Word align arg list */
  esp -= ((uintptr_t) esp) % sizeof(intptr_t);

  /* Terminate arg list with null */
  esp -= sizeof(intptr_t);

  /* Copy arg list to bottom of stack */
  esp -= len;
  memcpy(esp, pg_round_down(plist_end), len);

  /* Push argv */
  esp -= sizeof(char **);
  tmp = (char**) esp;
  *tmp = esp + sizeof(char **);

  /* Push argc */
  esp -= sizeof(int);
  *(int *)esp = len / sizeof(char *);

  /* Push fake return address */
  esp -= sizeof(void *);

  return esp;
}

/* File descriptor hashing functions */
static unsigned fd_hash_func(const struct hash_elem *e, void *aux UNUSED) {
  return hash_int(fd_elem_from_hash(e)->fd);
}

static bool fd_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
  return fd_elem_from_hash(a)->fd < fd_elem_from_hash(b)->fd;
}

static void close_fd_hash(struct hash_elem *e, void *aux UNUSED) {
  struct fd_elem *f = fd_elem_from_hash(e);

  lock_acquire(&fs_lock);
  file_close(f->file);
  lock_release(&fs_lock);
  free(f);
}

static inline struct fd_elem *fd_elem_from_hash(const struct hash_elem *e) {
  return hash_entry(e, struct fd_elem, hash_elem);
}

/* PID hashing functions. */
unsigned tid_hash(const struct hash_elem *e, void *aux UNUSED) {
  const struct exit_struct *ec = hash_entry(e, struct exit_struct, elem);
  return hash_int(ec->tid);
}

bool tid_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
  const struct exit_struct *a = hash_entry(a_, struct exit_struct, elem);
  const struct exit_struct *b = hash_entry(b_, struct exit_struct, elem);

  return a->tid < b->tid;
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
bool
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
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:/* We arrive here whether the load is successful or not. */
  /* Leave file open and deny writes to it.  Process_exit will close it. */
  thread_current()->executable = file;
  if(success)
    file_deny_write (file);

  /* If pagedir doesn't load, sema_up is never called in process_exit. */
  if(t->pagedir == NULL)
    sema_up(&t->exit_controler->parent);

  return success;
}

/* load() helpers. */

//static bool install_page (void *upage, void *kpage, bool writable);

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

//  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      //TODO this should all be in page.c
      struct page *page = page_alloc((void *) upage, PAGE_ONDISK);
      page->file = file;
      page->ofs = ofs;
      page->read_bytes = page_read_bytes;
      page->zero_bytes = page_zero_bytes;
      page->writeable = writable;

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += PGSIZE;
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

  struct page *page = page_alloc(PHYS_BASE - PGSIZE, PAGE_OTHER);
  page->writeable = true;
  page->zero_bytes = PGSIZE;
  *esp = PHYS_BASE;
  return true;
}