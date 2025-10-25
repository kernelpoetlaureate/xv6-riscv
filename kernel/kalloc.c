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

/*this is a function prototype declaration for freerange
//it tells the compiler that there is a function named freerange
// that takes two void pointer arguments and returns nothing (void)
//it contains 2 parameters: pa_start and pa_end, which are pointers to the start and end
// of the physical memory range to be freed. 

//pa_start points to the first address after the kernel code and data 
// in physical memory, 
//not the start of physical memory itself.

//pa_end points to the end of the physical memory range to be freed.


//according to the linker script, kernels code and data sections
//are stored in the interval 0x80000000 to 0x80023578
//the latter address is is calculated at link time, but 
//with current settings, it always ends up being 0x80023578

if we want highest possible memory layout view, 
this is how it looks like:

1. Before 0x80000000 - hard coded, fixed in stone area (I/O devices)
2. 0x80000000 to 0x80023578 - Kernel space
3. 0x80023578 to PHYSTOP - Free memory for allocation
*/

//here we just define the 'end' symbol, we will use it shortly. 
extern char end[];
//from now on, begins the sea of free memory , that we can allocate.


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

//below we call the freerange function, which actually does the freeing 
  freerange(end, (void*)PHYSTOP);

}
// now 


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

