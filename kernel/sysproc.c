#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "pageinfo.h"

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
  uint64 dst;
  int max;
  argaddr(0, &dst);
  argint(1, &max);
  if(max <= 0) return -1;

  return pageinfo_copy_to_user(dst, max);
}

// syscall pageinfo_va(dst_user_ptr, vaddr)
// copy the pageinfo entry for the physical page backing vaddr in the
// current process into user buffer at dst. Returns 0 on success.
uint64
sys_pageinfo_va(void)
{
  uint64 dst;
  uint64 va;
  argaddr(0, &dst);
  argaddr(1, &va);

  struct proc *p = myproc();
  if(!p) return -1;

  // find physical address backing this virtual address in this process
  uint64 pa = walkaddr(p->pagetable, va);
  if(pa == 0) return -1;

  if(pageinfo_copy_entry_to_user(dst, (void*)pa) < 0)
    return -1;
  return 0;
}
