#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"
#include "trace.h"

// Fetch the uint64 at addr from the current process.
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz) // both tests needed, in case of overflow
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// Fetch the nul-terminated string at addr from the current process.
// Returns length of string, not including nul, or -1 for error.
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// Fetch the nth 32-bit system call argument.
int
argint(int n, int *ip)
{
  *ip = argraw(n);
  return 0;
}

// Retrieve an argument as a pointer.
// Doesn't check for legality, since
// copyin/copyout will do that.
int
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
  return 0;
}

// Fetch the nth word-sized system call argument as a null-terminated string.
// Copies into buf, at most max.
// Returns string length if OK (including nul), -1 if error.
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_chdir(void);
extern uint64 sys_dup(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_pause(void);
extern uint64 sys_uptime(void);
extern uint64 sys_open(void);
extern uint64 sys_write(void);
extern uint64 sys_mknod(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_close(void);
extern uint64 sys_kread(void);
extern uint64 sys_pageinfo(void);
extern uint64 sys_pageinfo_va(void);
extern uint64 sys_pageinfo_phys(void);
extern uint64 sys_procstat(void);
extern uint64 sys_getrss(void);
extern uint64 sys_get_pagemap(void);
extern uint64 sys_trace_read(void);

// An array mapping syscall numbers from syscall.h
// to the function that handles the system call.
static uint64 (*syscalls[])(void) = {
  [SYS_fork]    = sys_fork,
  [SYS_exit]    = sys_exit,
  [SYS_wait]    = sys_wait,
  [SYS_pipe]    = sys_pipe,
  [SYS_read]    = sys_read,
  [SYS_kill]    = sys_kill,
  [SYS_exec]    = sys_exec,
  [SYS_fstat]   = sys_fstat,
  [SYS_chdir]   = sys_chdir,
  [SYS_dup]     = sys_dup,
  [SYS_getpid]  = sys_getpid,
  [SYS_sbrk]    = sys_sbrk,
  [SYS_pause]   = sys_pause,
  [SYS_uptime]  = sys_uptime,
  [SYS_open]    = sys_open,
  [SYS_write]   = sys_write,
  [SYS_mknod]   = sys_mknod,
  [SYS_unlink]  = sys_unlink,
  [SYS_link]    = sys_link,
  [SYS_mkdir]   = sys_mkdir,
  [SYS_close]   = sys_close,
  [SYS_kread]   = sys_kread,
  [SYS_pageinfo] = sys_pageinfo,
  [SYS_pageinfo_va] = sys_pageinfo_va,
  [SYS_pageinfo_phys] = sys_pageinfo_phys,
  [SYS_procstat] = sys_procstat,
  [SYS_getrss] = sys_getrss,
  [SYS_get_pagemap] = sys_get_pagemap,
  [SYS_trace_read] = sys_trace_read,
};

// Optional human-readable syscall names aligned with syscall numbers.
// syscall_names indexed by syscall number; index 0 is unused/placeholder.
/*
static const char *syscall_names[] = {
  "?",      // 0
  "fork",   // 1
  "exit",   // 2
  "wait",   // 3
  "pipe",   // 4
  "read",   // 5
  "kill",   // 6
  "exec",   // 7
  "fstat",  // 8
  "chdir",  // 9
  "dup",    // 10
  "getpid", // 11
  "sbrk",   // 12
  "pause",  // 13
  "uptime", // 14
  "open",   // 15
  "write",  // 16
  "mknod",  // 17
  "unlink", // 18
  "link",   // 19
  "mkdir",  // 20
  "close"   // 21
};
*/

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // Emit syscall enter trace (ring buffer)
    trace_emit(TRACE_SYSCALL_ENTER, num, p->trapframe->a0, p->trapframe->a1, p->trapframe->a2, p->sz, p->trapframe->sp);
    // Also print a short console line showing the syscall and its common args.
    printf("SYSCALL ENTRY: pid=%d name=%s num=%d a0=0x%lx a1=0x%lx a2=0x%lx\n",
           p->pid, p->name, num, p->trapframe->a0, p->trapframe->a1, p->trapframe->a2);

    // Call the syscall and store its return value in a0
    p->trapframe->a0 = syscalls[num]();
    // Emit syscall exit trace (ring buffer)
    trace_emit(TRACE_SYSCALL_EXIT, num, p->trapframe->a0, 0, 0, 0, 0);
    // And print the return value for immediate console debugging.
    printf("SYSCALL EXIT: pid=%d num=%d retval=%lu\n",
      p->pid, num, p->trapframe->a0);
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}

