#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "pageinfo.h"
extern pagetable_t kernel_pagetable;
#include "vm.h"
extern char end[]; // declared in kalloc.c; needed for phys memory checks

#include "procstat.h"

// Debug prints in this file can be enabled by defining PROCSTAT_DEBUG=1
// for example: make CFLAGS+=-DPROCSTAT_DEBUG=1
#ifndef PROCSTAT_DEBUG
#define PROCSTAT_DEBUG 0
#endif


uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    if(addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// Copy kernel virtual memory starting at addr to user-space buffer.
// syscall kread(addr, len, dstuser)
uint64
sys_kread(void)
{
  uint64 addr;
  int len;
  uint64 dst;

  argaddr(0, &addr);
  argint(1, &len);
  argaddr(2, &dst);

  // Security: limit reads to a single page (4096 bytes) to avoid abuse
  // and to keep the syscall simple. kread copies from kernel virtual
  // addresses (not raw physical addresses). If the caller needs to
  // inspect a physical page that is not mapped into the kernel virtual
  // address space, they should use the pageinfo helpers to discover a VA
  // mapping first or use higher-level facilities implemented in userland.
  if(len < 0 || len > 4096) // limit readers to a page to avoid abuse
    return -1;

  // either_copyout with user_dst=1 will copy from kernel addr to user dst
  if(either_copyout(1, dst, (char*)addr, (uint64)len) < 0)
    return -1;
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// Copy kernel pageinfo table into user-space buffer.
// syscall pageinfo(dst_user_ptr, max_entries)
uint64
sys_pageinfo(void)
{
  uint64 addr;
  int isphys;
  
  if(argaddr(0, &addr) < 0 || argint(1, &isphys) < 0)
    return -1;
  
  // Mode 2 is special - dump all page info
  //
  // Implementation notes for callers and maintainers:
  // - When `isphys == 2` the kernel prints a textual dump of the
  //   pageinfo table to the kernel console (QEMU serial). The printed
  //   lines correspond to entries in `pi_array` that are not PGTYPE_FREE.
  // - The dump iterates `pi_array` from low physical addresses to high,
  //   therefore the output is sorted by physical address (PA). Any
  //   observed gap between successive printed PA values larger than
  //   PGSIZE (0x1000) implies one or more pages in that physical range
  //   were omitted from the output (typically they are free / marked
  //   PGTYPE_FREE). The dump does not explicitly print free pages.
  // - The dump prints metadata only (pa/type/pid/va/tag/ref). It does
  //   not print page contents to avoid unsafe kernel memory reads.
  // - Because pageinfo is best-effort bookkeeping, `owner_pid`, `ref`,
  //   and `mapped_va` should be treated as heuristics for debugging,
  //   not authoritative allocator state.
  //
  // Example: in the dump a sequence of lines with PA stepping by 0x1000
  // represents contiguous allocated pages. A jump of 0x2000 means one
  // unprinted page between them; 0x3000 means two, etc.

  if(isphys == 2) {
    return dump_pageinfo();
  }
  
  // Normal page info modes for virtual/physical addresses not implemented yet
  return -1;
}

// syscall pageinfo_va(dst_user_ptr, vaddr)
// copy the pageinfo entry for the physical page backing vaddr in the
// current process into user buffer at dst. Returns 0 on success.
// Now supports looking up kernel virtual addresses as well.
uint64
sys_pageinfo_va(void)
{
  uint64 dst;
  uint64 va;
  argaddr(0, &dst);
  argaddr(1, &va);

  struct proc *p = myproc();
  if(!p) return -1;

  // First try to find in the current process pagetable
  uint64 pa = walkaddr(p->pagetable, va);
  
  // If not found in the process pagetable, try the kernel pagetable
  if(pa == 0) {
    pa = walkaddr_any(kernel_pagetable, va);
    if(pa == 0) return -1;  // Not found in either pagetable
  }

  if(pageinfo_copy_entry_to_user(dst, (void*)pa) < 0)
    return -1;
  return 0;
}

// Walk a target process pagetable and count resident pages within [0, p->sz).
uint64
sys_getrss(void)
{
  int pid;
  if(argint(0, &pid) < 0)
    return -1;

  struct proc *p = find_proc(pid);
  if(p == 0)
    return -1;

  // p->lock is held
  uint64 count = 0;
  for(uint64 va = 0; va < p->sz; va += PGSIZE){
    pte_t *pte = walk(p->pagetable, va, 0);
    if(pte && (*pte & PTE_V))
      count++;
  }

  release(&p->lock);
  return count * PGSIZE; // return bytes
}

// get_pagemap(pid, buf_addr, max)
// Fill up to `max` struct page_info entries into user buffer at buf_addr.
uint64
sys_get_pagemap(void)
{
  int pid;
  uint64 bufaddr;
  int max;
  if(argint(0, &pid) < 0 || argaddr(1, &bufaddr) < 0 || argint(2, &max) < 0)
    return -1;

  if(max <= 0)
    return -1;

  struct proc *p = find_proc(pid);
  if(p == 0)
    return -1;

  int written = 0;
  struct page_info pi;

  for(uint64 va = 0; va < p->sz && written < max; va += PGSIZE){
    pte_t *pte = walk(p->pagetable, va, 0);
    if(pte && (*pte & PTE_V)){
      uint64 pa = PTE2PA(*pte);
      pi.va = va;
      pi.pa = pa;
      pi.flags = (uint16)(*pte & 0x3FF); // low 10 bits
      pi.refcount = 0; // unknown unless page_refs implemented

      if(copyout(myproc()->pagetable, bufaddr + written * sizeof(pi), (char*)&pi, sizeof(pi)) < 0){
        release(&p->lock);
        return -1;
      }
      written++;
    }
  }

  release(&p->lock);
  return written;
}

// syscall pageinfo_phys(dst_user_ptr, phys_addr)
// Copy the pageinfo entry for the specified physical address directly.
// Returns 0 on success.
uint64
sys_pageinfo_phys(void)
{
  uint64 dst;
  uint64 pa;
  argaddr(0, &dst);
  argaddr(1, &pa);

  // Verify the physical address is within valid range
  if(pa < (uint64)end || pa >= PHYSTOP)
    return -1;

  // Make sure pa is page-aligned
  if((pa % PGSIZE) != 0)
    pa = PGROUNDDOWN(pa);

  if(pageinfo_copy_entry_to_user(dst, (void*)pa) < 0)
    return -1;
  return 0;
}

// syscall procstat(buf, max)
// Copy up to 'max' procstat entries into user buffer 'buf'.
uint64
sys_procstat(void)
{
  uint64 addr;
  int max;
  if(argaddr(0, &addr) < 0 || argint(1, &max) < 0)
    return -1;
  // Debug: log syscall arguments and caller
#if PROCSTAT_DEBUG
  struct proc *caller = myproc();
  if(caller)
    printf("sys_procstat: caller pid=%d name=\"%s\" addr=%p max=%d\n", caller->pid, caller->name, (void*)addr, max);
  else
    printf("sys_procstat: caller=NULL addr=%p max=%d\n", (void*)addr, max);
#endif

  struct proc *p;
  int count = 0;

  for(p = proc; p < &proc[NPROC] && count < max; p++){
    struct procstat ps;
    acquire(&p->lock);
    if(p->state == UNUSED){
      // produce an empty/placeholder entry for UNUSED slots
      ps.pid = 0;
      ps.state = UNUSED;
      ps.sz = 0;
      ps.total_ticks = 0;
      ps.ctime = 0;
      ps.etime = 0;
      ps.io_reads = 0;
      ps.io_writes = 0;
      ps.name[0] = '\0';
    } else {
      ps.pid = p->pid;
      ps.state = p->state;
      ps.sz = p->sz;
      ps.total_ticks = p->total_ticks;
      ps.ctime = p->ctime;
      ps.etime = p->etime;
      ps.io_reads = p->io_reads;
      ps.io_writes = p->io_writes;
      safestrcpy(ps.name, p->name, sizeof(ps.name));
    }
    release(&p->lock);

    uint64 dstva = addr + count * sizeof(ps);
    // Debug: show page mapping info for the destination virtual address
#if PROCSTAT_DEBUG
    uint64 va0 = PGROUNDDOWN(dstva);
    uint64 pa0 = 0;
    if(caller)
      pa0 = walkaddr(caller->pagetable, va0);
    printf("sys_procstat: dstva=%p va0=%p caller.sz=0x%lx caller.sp=0x%lx walk_pa=%p\n",
           (void*)dstva, (void*)va0, caller ? caller->sz : 0, caller ? caller->trapframe->sp : 0, (void*)pa0);
#endif

    if(copyout(myproc()->pagetable, dstva, (char*)&ps, sizeof(ps)) < 0) {
#if PROCSTAT_DEBUG
      printf("sys_procstat: copyout failed at idx=%d addr=%p (va0=%p pa0=%p)\n", count, (void*)dstva, (void*)va0, (void*)pa0);
#endif
      return -1;
    }

    count++;
  }

  return count;
}
