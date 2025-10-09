// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"
#include "pageinfo.h"

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

// forward declaration to avoid implicit-declaration when kinit calls freerange
void freerange(void *pa_start, void *pa_end);

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

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
    
  // Register the page as free
  register_page_free((uint64)pa);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

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

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    
    // Register this page allocation
    int pid = 0;
    struct proc *p = myproc();
    if(p)
      pid = p->pid;
    register_page_allocation((uint64)r, pid);
  }
  return (void*)r;
}

