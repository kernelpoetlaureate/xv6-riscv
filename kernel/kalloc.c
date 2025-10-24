// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h" // basic type definitions
#include "param.h" // system parameters
#include "memlayout.h" // memory layout
#include "spinlock.h" // spinlock definitions
#include "riscv.h" // RISC-V definitions
#include "defs.h" // kernel function definitions

void freerange(void *pa_start, void *pa_end);
//this is a function prototype declaration for freerange
//it tells the compiler that there is a function named freerange
// that takes two void pointer arguments and returns nothing (void)
//it contains 2 parameters: pa_start and pa_end, which are pointers to the start and end
// of the physical memory range to be freed. 

//pa_start points to the first address after the kernel code and data 
// in physical memory, 
//not the start of physical memory itself.

//pa_end points to the end of the physical memory range to be freed.


extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}
//

//this function defines and frees physical memory pages 
// between pa_start and pa_end

void
freerange(void *pa_start, void *pa_end) 
{
  char *p; //declares a pointer to char type, we use the pointer inside
           //the for loop below 
  p = (char*)PGROUNDUP((uint64)pa_start); //some rounding up operation
  

  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) { //marking the range
    kfree(p); //Free the current page
  }
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
  return (void*)r;
}
