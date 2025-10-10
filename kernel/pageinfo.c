// Simple pageinfo implementation storing minimal allocation info per phys page.

// Implementation notes (focus: dump semantics and gaps):
// - pageinfo is a best-effort debugging/inspection table. It mirrors
//   allocation and mapping events by observing hooks called from the
//   allocator (kalloc/kfree) and from mapping helpers (mappages etc.).
//   It is intentionally conservative and should NOT be used as the
//   authoritative allocator state.
//
// - The array `pi_array` is indexed by physical-page number (PA/PGSIZE).
//   The dump routine `dump_pageinfo()` iterates this array in index order
//   (low physical address to high). Therefore the dump output is sorted
//   by physical address (PA). Each printed line corresponds to one array
//   entry that is *not* PGTYPE_FREE.
//
// - Gaps in the dump (i.e., when successive printed PA values differ by
//   more than PGSIZE == 0x1000) indicate one or more physical pages that
//   are not currently tracked as in-use (typically free). The dump does
//   not explicitly print free pages; it omits PGTYPE_FREE entries. Thus
//   a larger-than-0x1000 delta between printed PAs means there are
//   unprinted pages between them (which were either freed or never
//   recorded).
//
// - Both PA and mapped VA fields are recorded as observed by the hooks.
//   If a page was allocated and immediately mapped at a VA, you will
//   often see PA and VA fields stepping by 0x1000 in sequence. If a page
//   was freed (kfree) it will be marked PGTYPE_FREE and won't appear in
//   the dump until reallocated and re-registered.
//
// - Limitations: the subsystem is not perfectly synchronized with every
//   allocator or VM event — `owner_pid`, `ref`, and `alloc_tick` are
//   heuristics useful for debugging but not authoritative.
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "pageinfo.h"

struct pageinfo pi_array[PI_NPAGES];
static struct {
  struct spinlock lock;
  int initialized;
} pi_state;

// Initialize the pageinfo subsystem.
// This creates the lock used to protect the pi_array and marks the
// subsystem initialized. Callers should check `pi_state.initialized`
// before attempting to read or write entries (some very early allocations
// may happen before initialization and thus won't be recorded). pageinfo
// is initialized late in kinit() after the allocator's free list is
// populated so that boot-time freed pages don't race with registration.
void
pageinfo_init(void)
{
  initlock(&pi_state.lock, "pageinfo");
  pi_state.initialized = 1;
}

void
register_page_allocation(uint64 pa, int pid)
{
  // If pageinfo isn't initialized yet, silently ignore. This ensures
  // boot-time allocator activity that occurs before pageinfo_init()
  // doesn't panic the kernel; those allocations simply won't be tracked.
  if(!pi_state.initialized)
    return;
  uint64 idx = PA2IDX((uint64)pa);
  if(idx >= PI_NPAGES){
    printf("register_page_allocation: pa %p is out of range\n", (void*)pa);
    return;
  }
  // Acquire lock and update the per-page metadata. This is a best-effort
  // recording: the allocator (kalloc) calls this to mark pages it hands
  // out. Other parts of the kernel may also update pageinfo via
  // pageinfo_set_mapped() when mappings are created. We record a small
  // allocation tag and a timestamp to help correlate runtime events.
  acquire(&pi_state.lock);
  // Best-effort: mark kernel pages when pid==0, otherwise user pages.
  if (pid == 0)
    pi_array[idx].type = PGTYPE_KERNEL;
  else
    pi_array[idx].type = PGTYPE_USER;
  pi_array[idx].owner_pid = pid;
  // tag the allocation source; keep it short
  if (pid == 0)
    safestrcpy(pi_array[idx].tag, "kalloc:ker", PAGEINFO_TAGLEN);
  else
    safestrcpy(pi_array[idx].tag, "kalloc", PAGEINFO_TAGLEN);
  pi_array[idx].mapped_va = 0;
  // record allocation time (best-effort): read global ticks under its lock
  {
    extern uint ticks;
    extern struct spinlock tickslock;
    uint t = 0;
    acquire(&tickslock);
    t = ticks;
    release(&tickslock);
    pi_array[idx].alloc_tick = t;
  }
  pi_array[idx].ref = 1;
  release(&pi_state.lock);
}

void
register_page_free(uint64 pa)
{
  // If pageinfo hasn't been initialized yet, there is nothing to clear.
  if(!pi_state.initialized)
    return;
  uint64 idx = PA2IDX(pa);
  if(idx >= PI_NPAGES){
    printf("register_page_free: pa %p is out of range\n", (void*)pa);
    return;
  }
  // Mark the tracked page as free. This clears the owner and mapping
  // metadata so dump/inspection tools won't treat it as in-use.
  acquire(&pi_state.lock);
  pi_array[idx].type = PGTYPE_FREE;
  pi_array[idx].owner_pid = 0;
  pi_array[idx].mapped_va = 0;
  pi_array[idx].tag[0] = '\0';
  pi_array[idx].alloc_tick = 0;
  pi_array[idx].ref = 0;
  release(&pi_state.lock);
}

int
dump_pageinfo(void)
{
  if(!pi_state.initialized) {
    printf("pageinfo not initialized\n");
    return -1;
  }
  int count = 0;
  acquire(&pi_state.lock);
  for(int i = 0; i < (int)PI_NPAGES; i++){
    if(pi_array[i].type != PGTYPE_FREE){
      printf("pa=0x%lx type=%d pid=%d va=0x%lx tag=%s ref=%lu\n",
        IDX2PA(i), pi_array[i].type, pi_array[i].owner_pid, pi_array[i].mapped_va, pi_array[i].tag, pi_array[i].ref);
      // (hexdump removed) If you want to inspect contents safely, use user tools
      // that copy pages into user-space via a safe syscall or request the kernel
      // to dump specific mapped virtual addresses. Dereferencing physical
      // addresses directly in the kernel may cause traps on unmapped ranges.
      count++;
    }
  }
  release(&pi_state.lock);
  return count;
}

int
pageinfo_copy_to_user(uint64 dst, int max)
{
  if(!pi_state.initialized) return -1;
  if(max <= 0) return -1;
  int tocopy = (max < (int)PI_NPAGES) ? max : (int)PI_NPAGES;
  for(int i = 0; i < tocopy; i++){
    if(either_copyout(1, dst + i * sizeof(struct pageinfo), (char*)&pi_array[i], sizeof(struct pageinfo)) < 0)
      return -1;
  }
  return tocopy;
}

int
pageinfo_copy_entry_to_user(uint64 dst, void *pa)
{
  if(!pi_state.initialized) return -1;
  uint64 idx = PA2IDX(pa);
  if(idx >= PI_NPAGES) return -1;
  if(either_copyout(1, dst, (char*)&pi_array[idx], sizeof(struct pageinfo)) < 0)
    return -1;
  return 0;
}

// record a mapping event for physical page pa mapped at virtual address va
// owner is pid (0 for kernel), and t is PGTYPE_*
void
pageinfo_set_mapped(void *pa, uint64 va, int owner, unsigned char t)
{
  if(!pi_state.initialized) return;
  uint64 idx = PA2IDX(pa);
  if(idx >= PI_NPAGES) return;
  acquire(&pi_state.lock);
  pi_array[idx].mapped_va = va;
  pi_array[idx].owner_pid = owner;
  pi_array[idx].type = t;
  // increase ref count to indicate an additional mapping (best-effort)
  pi_array[idx].ref++;
  release(&pi_state.lock);
}
