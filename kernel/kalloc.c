// Physical memory allocator.

// This simple allocator manages whole 4 KiB pages (PGSIZE) and is used for:
//  - user process memory (heap/stack)
//  - kernel stacks
//  - page-table pages (intermediate and root page table pages)
//  - buffer cache / pipe buffers
//
// Design and semantics (for maintainers):
// - The allocator represents free pages as a singly-linked list of
//   `struct run` attached to `kmem.freelist`. Allocation is LIFO: pages are
//   popped from the head of the list by `kalloc()` and pushed back by
//   `kfree()`.
// - The free-list is protected by `kmem.lock` (a spinlock). All accesses to
//   the linked list must hold that lock.
// - At boot `kinit()` calls `freerange(end, PHYSTOP)` which uses `kfree()` to
//   add every page in the available RAM range to the free-list. `end` is the
//   first address after the kernel image (set by the linker), and `PHYSTOP`
//   marks the top of usable physical memory. This effectively initializes the
//   pool of pages the kernel can allocate from at runtime.
// - Callers allocate pages by calling `kalloc()`. Many higher-level helpers
//   (walk()/mappages()/uvmalloc()/uvmcopy()/vmfault()) call `kalloc()` to
//   obtain pages for page tables and user memory. `kalloc()` records
//   allocations via `register_page_allocation()` for debugging.
// - Pages are filled with a byte pattern on allocation (memset with 5) and
//   with a different pattern on free (memset with 1). Higher-level code
//   commonly zeroes pages immediately after allocation before use (e.g.
//   uvmalloc() and walk() do memset to 0), so the filler is mainly a debug
//   aid to catch use-after-free or uninitialized reads.

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
// Enable boot logging to make early allocator activity visible on the
// console. This helps trace what happens before kernel stack mappings
// (proc_mapstacks) are printed.
static int kmem_log_boot = 1;       // set to 1 to enable boot logging
static int kmem_initializing = 0;   // true while kinit/freerange runs
static uint64 kmem_freed_pages = 0;
static uint64 kmem_initial_pages = 0; // total pages expected to be freed during init

void
kinit()
{
  // Initialize the spinlock for the kmem structure to ensure thread-safe access.
  initlock(&kmem.lock, "kmem");

  // If boot-time logging is enabled, set the initializing flag and log the memory range.
  if(kmem_log_boot) {
    kmem_initializing = 1; // Indicate that initialization is in progress.
    printf("kinit: freerange from %p to %p\n", end, (void*)PHYSTOP); // Log the memory range being initialized.
  }

  // Populate the free-list with all available physical memory pages in the range.
  freerange(end, (void*)PHYSTOP);

  // If boot-time logging is enabled, clear the initializing flag and log the number of freed pages.
  if(kmem_log_boot) {
    kmem_initializing = 0; // Indicate that initialization is complete.
    printf("kinit: finished freerange; freed pages=%lu\n", kmem_freed_pages); // Log the total freed pages.
  }

  // Initialize the pageinfo subsystem after the free-list has been fully populated.
  pageinfo_init();
}

void
freerange(void *pa_start, void *pa_end)
{
  // If boot-time logging is enabled and initialization is in progress, log the range being freed.
  if(kmem_log_boot && kmem_initializing)
    printf("freerange: pa_start=%p pa_end=%p\n", pa_start, pa_end);

  char *p;
  // Round up the starting physical address to the nearest page boundary.
  uint64 start_pa = PGROUNDUP((uint64)pa_start);

  // If boot-time logging is enabled and initialization is in progress, calculate the total pages to be freed.
  if(kmem_log_boot && kmem_initializing)
    kmem_initial_pages = (((uint64)pa_end) - start_pa) / PGSIZE;

  // Start freeing pages from the rounded-up starting address to the end address.
  p = (char*)start_pa;
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p); // Free each page and add it to the free-list.
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
    
  // Register the page as free in the pageinfo debugging table. This will
  // mark the page as PGTYPE_FREE and clear its metadata so dump/inspection
  // tools don't show it as in-use.
  register_page_free((uint64)pa);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
#if 1
  // If we are in boot initialization, account for freed pages so the
  // kinit() summary can report how many pages were added to the free list.
  if(kmem_initializing)
    kmem_freed_pages++;

  // Print a progress dot every 4096 freed pages to show activity without
  // overwhelming the console. This is useful when running on large RAM
  // sizes or slow consoles.
  if(kmem_log_boot && kmem_initializing && (kmem_freed_pages % 4096) == 0)
    printf("kinit: freed pages so far=%lu\n", kmem_freed_pages);
#endif
#ifdef KDEBUG_MEM
  printf("KFREE pa=0x%lx\n", (uint64)pa);
#endif
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
    // Fill with a recognizable pattern to help detect uninitialized
    // accesses. Most callers will zero the page before use, so this is
    // primarily for debugging small windows where uninitialized reads
    // might occur.
    memset((char*)r, 5, PGSIZE);

    // Register this page allocation in the pageinfo table so that
    // debugging/inspection tools can attribute the page to the current
    // process (or to the kernel if myproc() is NULL).
    int pid = 0;
    struct proc *p = myproc();
    if(p)
      pid = p->pid;
    register_page_allocation((uint64)r, pid);
  #ifdef KDEBUG_MEM
    if(pid)
      printf("KALLOC pid=%d pa=0x%lx\n", pid, (uint64)r);
    else
      printf("KALLOC kernel pa=0x%lx\n", (uint64)r);
  #endif
  }
  return (void*)r;
}

