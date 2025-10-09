// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "pageinfo.h"
#include "proc.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// Enable automatic boot-time free-list logging by default, but
// only while the allocator is being initialized. This keeps
// normal kfree/kalloc quiet after boot.
static int kmem_log_boot = 1;       // set to 0 to disable boot logging
static int kmem_initializing = 0;   // true while kinit/freerange runs
static uint64 kmem_freed_pages = 0;
static uint64 kmem_initial_pages = 0; // total pages expected to be freed during init

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  if(kmem_log_boot) {
    kmem_initializing = 1;
    printf("kinit: freerange from %p to %p\n", end, (void*)PHYSTOP);
  }
  freerange(end, (void*)PHYSTOP);
  if(kmem_log_boot) {
    kmem_initializing = 0;
    printf("kinit: finished freerange; freed pages=%lu\n", kmem_freed_pages);
  }
  // initialize pageinfo subsystem after free list is built
  pageinfo_init();
}

void
freerange(void *pa_start, void *pa_end)
{
  if(kmem_log_boot && kmem_initializing)
    printf("freerange: pa_start=%p pa_end=%p\n", pa_start, pa_end);
  char *p;
  uint64 start_pa = PGROUNDUP((uint64)pa_start);
  if(kmem_log_boot && kmem_initializing)
    kmem_initial_pages = (((uint64)pa_end) - start_pa) / PGSIZE;
  p = (char*)start_pa;
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  if(kmem_initializing) {
    /* Always count freed pages during initialization */
    kmem_freed_pages++;

    if(kmem_log_boot) {
      /*
       * Print only a concise selection: the first HEAD_PRINT pages,
       * a few evenly spaced middle samples, and the last LAST_PRINT pages.
       * This keeps boot logging to a few dozen lines instead of thousands.
       */
      const uint64 HEAD_PRINT = 8;
      const uint64 LAST_PRINT = 8;
      const uint64 MIDDLE_SAMPLES = 8; /* number of middle samples */

      uint64 idx = kmem_freed_pages - 1; /* 0-based index of this freed page */
      int do_print = 0;

      if(kmem_initial_pages > 0) {
        if(idx < HEAD_PRINT)
          do_print = 1;
        else if(idx >= kmem_initial_pages - LAST_PRINT)
          do_print = 1;
        else {
          /* pick a few middle samples evenly spaced */
          uint64 slots = MIDDLE_SAMPLES + 2; /* include head/tail in spacing calc */
          if(kmem_initial_pages > slots) {
            uint64 stride = kmem_initial_pages / slots;
            if (stride > 0 && (idx % stride) == 0)
              do_print = 1;
          }
        }
      } else {
        /* fallback: small amount of logging */
        if(kmem_freed_pages <= HEAD_PRINT)
          do_print = 1;
      }

      if(do_print)
        printf("kfree: freeing page at %p\n", pa);
    }
  }

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  // record allocation in pageinfo (owner is current proc if any, else kernel pid 0)
  int owner = 0;
  struct proc *p = myproc();
  if(p) owner = p->pid;

  // Use more detailed tagging to identify kernel subsystem
  const char* tag = "kalloc";
  
  // Check if we're in a specific kernel context to provide better tagging
  if (p == 0) {
    // No process context - early boot or pure kernel
    tag = "kernel-core";
  } else if (p->pid == 0) {
    // Init process
    tag = "kernel-init";
  } else if (p->name[0] != 0) {
    // Try to identify kernel subsystem based on the process name
    if (strncmp(p->name, "sh", 2) == 0)
      tag = "shell";
    else if (strncmp(p->name, "init", 4) == 0)
      tag = "init";
    else if (strncmp(p->name, "cat", 3) == 0)
      tag = "cat";
    else if (strncmp(p->name, "ls", 2) == 0)
      tag = "ls";
    // Add more common process names if needed
  }
  
  // Set the page type more precisely
  unsigned char type = PGTYPE_KERNEL;
  if(p && p->pid > 0)
    type = PGTYPE_USER;
  
  pageinfo_set_alloc((void*)r, type, owner, tag);
  return (void*)r;
}
