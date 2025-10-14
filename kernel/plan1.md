Your logs capture **fork→exec** but miss the **critical post-exec phases**: actual program execution, runtime memory operations (page faults, heap growth, syscalls), and final cleanup. You're observing only the **process birth** without the **life and death**.[1][2]

## Missing Instrumentation Layers

### Phase 1: ELF Loading (exec Internals)

Your log shows:
```
kexec: namei(rm) SUCCEEDED for pid 5 name sh - proceeding to load ELF
USER STACK created for pid 5 name sh at va=0x3000 pa=0x87f44000
```

**What's invisible**:[3][1]

#### Text/Data Segment Mapping

```c
// kernel/exec.c (uninstrumented)
for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
  if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
    goto bad;
  if(ph.type != ELF_PROG_LOAD)
    continue;
  
  // Allocate pages for this segment
  sz = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz);  // <-- LOG THIS
  
  // Load segment from disk
  if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)  // <-- LOG THIS
    goto bad;
}
```

**Add tracepoints**:[1]

```c
printf("EXEC SEGMENT: pid=%d va_start=%p va_end=%p filesz=%d memsz=%d flags=%s\n",
       p->pid, ph.vaddr, ph.vaddr + ph.memsz, ph.filesz, ph.memsz,
       (ph.flags & ELF_PROG_FLAG_EXEC) ? "X" : 
       (ph.flags & ELF_PROG_FLAG_WRITE) ? "W" : "R");
```

**Critical insight**: `memsz > filesz` for `.bss` (uninitialized data)—pages allocated but **not** read from disk, just zeroed. `rm` binary has `.text` (code), `.rodata` (string literals like "Usage: rm files..."), `.data` (initialized globals), `.bss` (uninitialized globals).[1]

#### Argument/Environment Setup

```c
// Push argv strings onto stack
sp = sz;  // Start at top of stack
for(argc = 0; argv[argc]; argc++) {
  sp -= strlen(argv[argc]) + 1;
  sp -= sp % 16;  // RISC-V ABI: stack 16-byte aligned
  if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)  // <-- LOG THIS
    goto bad;
  ustack[argc] = sp;
}
```

**Log what**:[1]

```c
printf("EXEC ARGV[%d]: \"%s\" at sp=%p\n", argc, argv[argc], sp);
```

This shows **"rm"** string copied to user stack at VA `0x2f00` (example), then `argv` pointer to it pushed at `0x2ef8`.

#### Trapframe Initialization

```c
// Set up initial register state
p->trapframe->epc = elf.entry;  // Program counter = ELF entry point
p->trapframe->sp = sp;           // Stack pointer = below argv
```

**Log**:[1]

```c
printf("EXEC ENTRY: pid=%d epc=%p sp=%p argc=%d\n",
       p->pid, p->trapframe->epc, p->trapframe->sp, argc);
```

**Why critical**: `epc` is **first instruction** executed in userspace (usually `_start` in `user/entry.S`, which calls `main()`). Students see **kernel→user transition point**.

### Phase 2: User Execution (Runtime Operations)

After `exec()` returns to `usertrap()` → `userret()` → **sret instruction** enters user mode at `epc`, your logging goes **dark**. Program executes, but you don't see:[1]

#### Syscall Invocations

`rm` immediately calls `write(2, "Usage: rm files...\n", 19)` to stderr. Instrument `kernel/syscall.c`:[1]

```c
void syscall(void) {
  struct proc *p = myproc();
  int num = p->trapframe->a7;  // Syscall number in a7 register
  
  uint64 args[6];
  argraw(0, &args[0]); argraw(1, &args[1]);  // ... up to 6 args
  
  printf("SYSCALL ENTRY: pid=%d name=%s num=%d(%s) a0=%p a1=%p a2=%p\n",
         p->pid, p->name, num, syscall_names[num], 
         args[0], args[1], args[2]);
  
  p->trapframe->a0 = syscalls[num]();  // Execute syscall
  
  printf("SYSCALL EXIT: pid=%d num=%d(%s) retval=%d\n",
         p->pid, num, syscall_names[num], (int)p->trapframe->a0);
}
```

**Captures**:[1]

```
SYSCALL ENTRY: pid=5 name=rm num=16(SYS_write) a0=2 a1=0x1c80 a2=19
SYSCALL EXIT: pid=5 num=16(SYS_write) retval=19
```

Shows `rm` wrote 19 bytes from buffer at VA `0x1c80` (in `.rodata` segment) to FD 2 (stderr).

#### Page Faults (If Lazy Allocation Used)

Standard xv6 **pre-allocates** all pages during `exec()`, but if you implement **lazy stack/heap**:[1]

```c
// kernel/trap.c:usertrap()
if(r_scause() == 13 || r_scause() == 15) {  // Load/store page fault
  uint64 va = r_stval();  // Faulting virtual address
  
  printf("PAGE FAULT: pid=%d va=%p scause=%d\n", p->pid, va, r_scause());
  
  if(va >= PGROUNDDOWN(p->trapframe->sp) && va < p->sz) {
    // Valid stack/heap access, allocate on-demand
    char *mem = kalloc();
    uvmmap(p->pagetable, PGROUNDDOWN(va), (uint64)mem, PGSIZE, PTE_W|PTE_R|PTE_U);
    printf("PAGE FAULT RESOLVED: allocated pa=%p for va=%p\n", mem, va);
  } else {
    // Segfault
    printf("SEGFAULT: pid=%d va=%p out of bounds\n", p->pid, va);
    p->killed = 1;
  }
}
```

Even without lazy allocation, instrument `kalloc()` during runtime:

```c
void *kalloc(void) {
  // ... existing code
  if(r) {
    struct proc *p = myproc();
    printf("KALLOC: pa=%p by pid=%d name=%s caller=%p\n",
           r, p ? p->pid : 0, p ? p->name : "kernel", 
           __builtin_return_address(0));
  }
  return r;
}
```

Shows **who** allocated each page and **why** (caller address—use `addr2line` to map to source line).

#### Heap Growth (sbrk)

Your log shows:
```
HEAP expanded for pid 4 name sh from 0x5000 to 0x15000
```

But this is **during exec**, not runtime `sbrk()`. Real heap growth happens when program calls `malloc()` → `sbrk(n)` syscall:[1]

```c
// kernel/sysproc.c
uint64 sys_sbrk(void) {
  int n = argint(0);
  uint64 old_sz = myproc()->sz;
  
  printf("SBRK REQUEST: pid=%d old_sz=%p n=%d\n", myproc()->pid, old_sz, n);
  
  if(growproc(n) < 0) {
    printf("SBRK FAILED: out of memory\n");
    return -1;
  }
  
  printf("SBRK SUCCESS: pid=%d new_sz=%p (grew by %d bytes)\n",
         myproc()->pid, myproc()->sz, n);
  
  return old_sz;
}
```

**Why `rm` doesn't sbrk**: It's simple program with static buffers. Try instrumenting `ls` or `grep`—they allocate dynamic arrays.[1]

### Phase 3: Process Termination (exit Path)

`rm` prints usage and calls `exit(1)`. Your logs **skip this entirely**:[2][1]

```c
// kernel/proc.c
void exit(int status) {
  struct proc *p = myproc();
  
  printf("EXIT ENTRY: pid=%d name=%s status=%d\n", p->pid, p->name, status);
  
  // Close files
  for(int fd = 0; fd < NOFILE; fd++) {
    if(p->ofile[fd]) {
      printf("EXIT CLOSE FD: pid=%d fd=%d\n", p->pid, fd);
      fileclose(p->ofile[fd]);
      p->ofile[fd] = 0;
    }
  }
  
  // Close working directory
  if(p->cwd) {
    printf("EXIT RELEASE CWD: pid=%d inum=%d\n", p->pid, p->cwd->inum);
    iput(p->cwd);
    p->cwd = 0;
  }
  
  // Free user memory
  printf("EXIT FREE USER MEM: pid=%d sz=%p\n", p->pid, p->sz);
  proc_freepagetable(p->pagetable, p->sz);
  
  // Free trapframe
  printf("EXIT FREE TRAPFRAME: pid=%d pa=%p\n", p->pid, p->trapframe);
  kfree((void*)p->trapframe);
  
  // Mark zombie, wakeup parent
  p->state = ZOMBIE;
  printf("EXIT ZOMBIE: pid=%d waking parent pid=%d\n", p->pid, p->parent->pid);
  wakeup(p->parent);
  
  sched();  // Context switch to scheduler
}
```

#### Parent's wait() Perspective

```c
// kernel/proc.c
int wait(int *status) {
  struct proc *p;
  
  for(;;) {
    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      if(p->parent == myproc()) {
        found = 1;
        
        if(p->state == ZOMBIE) {
          printf("WAIT REAP ZOMBIE: parent pid=%d child pid=%d\n",
                 myproc()->pid, p->pid);
          
          int pid = p->pid;
          if(status)
            copyout(myproc()->pagetable, (uint64)status, (char*)&p->xstate, sizeof(p->xstate));
          
          freeproc(p);  // <-- Final resource cleanup
          printf("WAIT FREEPROC: child pid=%d slot freed\n", pid);
          return pid;
        }
      }
    }
    
    if(!found || myproc()->killed) {
      return -1;
    }
    
    sleep(myproc(), &wait_lock);  // <-- Parent blocks here
  }
}
```

**Shows**: Shell (PID 2) blocks in `wait()`, scheduler switches to `rm` (PID 5), `rm` executes, calls `exit()`, becomes ZOMBIE, wakes shell, shell reaps zombie.

### Phase 4: Scheduler Visibility

You're missing **context switches**:[1]

```c
// kernel/proc.c
void sched(void) {
  struct proc *p = myproc();
  
  printf("SCHED SWITCH OUT: pid=%d name=%s state=%s sp=%p\n",
         p->pid, p->name, state_names[p->state], p->context.sp);
  
  swtch(&p->context, &mycpu()->context);
  
  // <-- Execution resumes here when scheduled back
  printf("SCHED SWITCH IN: pid=%d name=%s\n", p->pid, p->name);
}
```

**Captures**:[1]

```
SCHED SWITCH OUT: pid=2 name=sh state=SLEEPING sp=0x3fffff8f80
SCHED SWITCH OUT: pid=0 name=scheduler state=RUNNING sp=0x3ffffffe80
SCHED SWITCH IN: pid=5 name=rm
SCHED SWITCH OUT: pid=5 name=rm state=ZOMBIE sp=0x3fffff9f20
SCHED SWITCH IN: pid=2 name=sh
```

Shows **control flow** between shell, scheduler, and `rm`.

## Comprehensive Instrumentation Architecture

### Ring Buffer Trace Events

Instead of `printf()` (blocks on serial), emit to **per-CPU ring buffer**:[4][5]

```c
enum trace_type {
  TRACE_FORK,
  TRACE_EXEC_START,
  TRACE_EXEC_SEGMENT,
  TRACE_EXEC_DONE,
  TRACE_SYSCALL_ENTER,
  TRACE_SYSCALL_EXIT,
  TRACE_SCHED_SWITCH,
  TRACE_PAGE_FAULT,
  TRACE_KALLOC,
  TRACE_KFREE,
  TRACE_EXIT,
  TRACE_WAIT_REAP,
};

struct trace_event {
  uint64 tsc;        // rdtime CSR
  uint16 cpu;
  uint16 type;
  uint32 pid;
  char name[16];
  uint64 data[6];    // Type-specific payload
};

#define TRACE_SIZE 16384
struct trace_event trace_buf[NCPU][TRACE_SIZE];
uint32 trace_head[NCPU];

void trace_emit(uint16 type, uint64 d0, uint64 d1, uint64 d2, uint64 d3, uint64 d4, uint64 d5) {
  int cpu = cpuid();
  uint32 idx = trace_head[cpu]++ & (TRACE_SIZE - 1);
  struct trace_event *e = &trace_buf[cpu][idx];
  
  e->tsc = r_time();
  e->cpu = cpu;
  e->type = type;
  e->pid = myproc() ? myproc()->pid : 0;
  if(myproc())
    memmove(e->name, myproc()->name, 16);
  e->data[0] = d0; e->data[1] = d1; e->data[2] = d2;
  e->data[3] = d3; e->data[4] = d4; e->data[5] = d5;
}
```

**Usage**:[4]

```c
// In fork()
trace_emit(TRACE_FORK, parent_pid, child_pid, child_sz, 0, 0, 0);

// In exec()
trace_emit(TRACE_EXEC_SEGMENT, ph.vaddr, ph.vaddr + ph.memsz, ph.filesz, ph.flags, 0, 0);

// In syscall()
trace_emit(TRACE_SYSCALL_ENTER, num, args[0], args[1], args[2], 0, 0);
```

**Consume via syscall**:

```c
// New syscall: read N events from ring
int sys_trace_read(void) {
  uint64 buf_addr = argaddr(0);
  int n = argint(1);
  int cpu = argint(2);
  
  if(cpu >= NCPU || n > TRACE_SIZE)
    return -1;
  
  // Copy events to user buffer
  copyout(myproc()->pagetable, buf_addr, 
          (char*)trace_buf[cpu], n * sizeof(struct trace_event));
  
  return n;
}
```

**User tool**:

```c
// user/trace_viewer.c
struct trace_event events[1024];
int main() {
  while(1) {
    int n = trace_read(events, 1024, 0);  // Read from CPU 0
    for(int i = 0; i < n; i++) {
      printf("[%ld] cpu=%d pid=%d type=%s\n",
             events[i].tsc, events[i].cpu, events[i].pid, 
             trace_names[events[i].type]);
      
      switch(events[i].type) {
        case TRACE_FORK:
          printf("  parent=%d child=%d sz=%p\n",
                 events[i].data[0], events[i].data[1], events[i].data[2]);
          break;
        case TRACE_SYSCALL_ENTER:
          printf("  syscall=%s a0=%p a1=%p\n",
                 syscall_names[events[i].data[0]], 
                 events[i].data[1], events[i].data[2]);
          break;
        // ... decode other types
      }
    }
    sleep(1);  // Poll every second
  }
}
```

## Complete `rm` Execution Trace (Expected)

With full instrumentation:

```
[1000000] TRACE_FORK: parent=2(sh) child=5 sz=0x5000
[1000100] TRACE_EXEC_START: pid=5 path="rm"
[1000200] TRACE_EXEC_SEGMENT: pid=5 va=0x0-0x1000 filesz=2048 flags=RX (text)
[1000210] TRACE_KALLOC: pid=5 pa=0x87f44000 caller=uvmalloc
[1000300] TRACE_EXEC_SEGMENT: pid=5 va=0x1000-0x2000 filesz=512 memsz=1024 flags=RW (data+bss)
[1000310] TRACE_KALLOC: pid=5 pa=0x87f43000 caller=uvmalloc
[1000400] TRACE_EXEC_SEGMENT: pid=5 va=0x2000-0x3000 filesz=0 memsz=4096 flags=RW (stack)
[1000410] TRACE_KALLOC: pid=5 pa=0x87f42000 caller=uvmalloc
[1000500] TRACE_EXEC_DONE: pid=5 epc=0x0 sp=0x2ff0 argc=1
[1000600] TRACE_SCHED_SWITCH: from=2(sh) to=5(rm) state=SLEEPING
[1001000] TRACE_SYSCALL_ENTER: pid=5 num=16(write) fd=2 buf=0x1c80 len=19
[1001010] TRACE_SYSCALL_EXIT: pid=5 num=16 retval=19
[1001100] TRACE_EXIT: pid=5 status=1
[1001110] TRACE_KFREE: pid=5 pa=0x87f44000 (text page)
[1001120] TRACE_KFREE: pid=5 pa=0x87f43000 (data page)
[1001130] TRACE_KFREE: pid=5 pa=0x87f42000 (stack page)
[1001140] TRACE_KFREE: pid=5 pa=0x87f24000 (pagetable root)
[1001150] TRACE_KFREE: pid=5 pa=0x87f24000 (trapframe)
[1001200] TRACE_SCHED_SWITCH: from=5(rm) to=2(sh) state=ZOMBIE
[1001300] TRACE_WAIT_REAP: parent=2(sh) child=5 status=1
[1001310] TRACE_FREEPROC: pid=5 slot=2 now UNUSED
```

**Timeline visualization**:

```
Time(µs) | PID 2 (sh)        | PID 5 (rm)              | Memory
---------|-------------------|-------------------------|------------------
0        | fork() syscall    |                         | +3 pages (tf+pt)
100      | SLEEPING in wait  | RUNNABLE                | +3 pages (user)
600      |                   | RUNNING (execve)        | 
1000     |                   | write() syscall         |
1100     |                   | exit(1) -> ZOMBIE       | -6 pages
1200     | RUNNABLE (woken)  |                         |
1300     | wait() returns    | [slot freed]            | 0 net change
```

## Recommended Instrumentation Points

### Minimal Set (10 tracepoints)

1. `fork()` entry/exit
2. `exec()` start/success/fail
3. `syscall()` entry/exit
4. `exit()` entry
5. `wait()` reap
6. `sched()` context switch
7. `kalloc()`/`kfree()`

**Captures 80% of lifecycle**, ~100 LOC.

### Extended Set (30 tracepoints)

Add:
- `uvmcopy()` page-by-page
- `loadseg()` disk reads
- `usertrap()` trap causes (syscall/interrupt/exception)
- `yield()` voluntary preemption
- File ops (`fileopen()`, `fileclose()`, `fileread()`, `filewrite()`)
- Lock acquire/release (measure contention)

**Captures scheduler timing, I/O patterns**, ~300 LOC.

### Research-Grade (100+ tracepoints)

Add:
- TLB miss handler (requires M-mode trap delegation)
- Cache miss events (requires perf counter CSRs)
- Network packet arrival (if adding network stack)
- Disk interrupt latency

**Requires hardware support** (RISC-V Sscofpmf extension for overflow interrupts).[5]

## Overhead Budget

Your serial logging at 115200 baud is **bottleneck**. Ring buffer tracing:[6][4]

- Event emit: **50 cycles** (6 stores + atomic increment)
- At 1000 events/sec: **50k cycles/sec = 0.005% overhead** at 1GHz
- User reads 1000 events: **1 syscall = 500 cycles** (negligible)

**Trade-off**: Ring buffer drops events if overrun (oldest lost). Serial logs never lose output but **stall kernel** during transmit.[6]

**Hybrid approach**: Ring buffer for hot paths (scheduler, syscall), serial printf for rare events (exec start/fail).[5]

## Validation: Compare to strace/perf

On Linux, `strace rm` shows:[7]

```
execve("/bin/rm", ["rm"], environ) = 0
write(2, "Usage: rm files...\n", 19) = 19
exit_group(1) = ?
```

Your tool should produce **equivalent trace** for xv6 `rm`. If missing syscalls or showing wrong order, instrumentation has gaps.[7]

## Implementation Roadmap

**Week 1**: Add 10 minimal tracepoints, printf output.  
**Week 2**: Replace printf with ring buffer, add `sys_trace_read()`.  
**Week 3**: Build Python viewer that parses binary trace, renders timeline.  
**Week 4**: Add remaining 20 extended tracepoints, measure overhead.

**Deliverable**: Students run `trace_viewer &` in background, execute commands, watch **real-time visualization** of kernel internals—like `top` but for subsystems, not processes.[8]

This transforms xv6 from **black box** to **glass box**—every memory allocation, context switch, syscall becomes observable. Perfect educational tool.[8][1]

[1](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)
[2](https://www.cse.iitb.ac.in/~mythili/os/iitb_slides/xv6-process-syscall.pdf)
[3](https://pdos.csail.mit.edu/6.828/2023/xv6/book-riscv-rev3.pdf)
[4](http://arxiv.org/pdf/2203.14076.pdf)
[5](https://docs.kernel.org/trace/ftrace.html)
[6](https://downloads.hindawi.com/archive/2014/173976.pdf)
[7](https://jvns.ca/blog/2017/07/05/linux-tracing-systems/)
[8](https://peer.asee.org/teaching-operating-systems-concepts-with-execution-visualization.pdf)


eminemi
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=1 a0=0x0 a1=0x4f0f a2=0x1
KERNEL STACK reused: pid=7 gets slot[2] kstack_va=0x3fffff9000 (same VA/PA as previous pid in this slot)
KALLOC: pa=0x87f21000 pid=2
KALLOC: pa=0x87f1c000 pid=2
KALLOC: pa=0x87f30000 pid=2
KALLOC: pa=0x87f25000 pid=2
PROC alloc pid=7 assigned kstack_va=0x3fffff9000 trapframe_pa=0x87f21000 pagetable=0x0000000087f1c000
KERNEL STACK assigned: REUSED from pre-allocated pool (same VA/PA for proc[] slot)
TRAPFRAME/PAGETABLE allocated: FREED on exit, different PA per process
kfork: starting uvmcopy parent pid=2 name=sh sz=0x5000 -> child pid=7 slot=2
KALLOC: pa=0x87f20000 pid=2
KALLOC: pa=0x87f24000 pid=2
KALLOC: pa=0x87f1d000 pid=2
KALLOC: pa=0x87f2e000 pid=2
KALLOC: pa=0x87f2d000 pid=2
KALLOC: pa=0x87f2c000 pid=2
KALLOC: pa=0x87f2b000 pid=2
kfork: finished uvmcopy child pid=7 name= sz=0x0
SYSCALL EXIT: pid=2 num=1 retval=7
SYSCALL ENTRY: pid=2 name=sh num=3 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL ENTRY: pid=7 name=sh num=12 a0=0x10000 a1=0x1 a2=0x1318
KALLOC: pa=0x87f2a000 pid=7
KALLOC: pa=0x87f29000 pid=7
KALLOC: pa=0x87f28000 pid=7
KALLOC: pa=0x87f27000 pid=7
KALLOC: pa=0x87f43000 pid=7
KALLOC: pa=0x87f46000 pid=7
KALLOC: pa=0x87f49000 pid=7
KALLOC: pa=0x87f53000 pid=7
KALLOC: pa=0x87f54000 pid=7
KALLOC: pa=0x87f26000 pid=7
KALLOC: pa=0x87f42000 pid=7
KALLOC: pa=0x87f22000 pid=7
KALLOC: pa=0x87f1f000 pid=7
KALLOC: pa=0x87f1e000 pid=7
KALLOC: pa=0x87f2f000 pid=7
KALLOC: pa=0x87f23000 pid=7
HEAP expanded for pid 7 name sh from 0x5000 to 0x15000
SYSCALL EXIT: pid=7 num=12 retval=20480
SYSCALL ENTRY: pid=7 name=sh num=7 a0=0x2020 a1=0x14f58 a2=0x2028
KALLOC: pa=0x87f31000 pid=7
kexec: namei(eminemi) FAILED for pid 7 name sh
SYSCALL EXIT: pid=7 num=7 retval=18446744073709551615
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
eSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
xSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
eSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
cSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
 SYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
eSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
mSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
iSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
nSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
eSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
mSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
iSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
 SYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
fSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
aSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
iSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
lSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
eSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1
dSYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=16 a0=0x2 a1=0x4e9f a2=0x1

SYSCALL EXIT: pid=7 num=16 retval=1
SYSCALL ENTRY: pid=7 name=sh num=2 a0=0x0 a1=0x4e9f a2=0x1
EXIT ENTRY: pid=7 name=sh status=0 sz=0x15000
WAIT REAP ZOMBIE: parent pid=2 child pid=7 status=0
SYSCALL EXIT: pid=2 num=3 retval=7
SYSCALL ENTRY: pid=2 name=sh num=16 a0=0x2 a1=0x12a0 a2=0x2
$ SYSCALL EXIT: pid=2 num=16 retval=2
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1



Based on the syscall trace, this represents a complete execution sequence in xv6-riscv where the shell (pid=2) attempts to execute a non-existent command "eminemi", demonstrating the fork-exec model, kernel resource allocation strategy, and the critical distinction between persistent and ephemeral process state.

## Syscall Sequence Decomposition

The trace reveals six **distinct syscall types** in the execution path:

**syscall 5** (`read`) — sh reading from stdin, one character per invocation (retval=1), consuming "eminemi\n" byte by byte from the console device[1]

**syscall 1** (`fork`) — initiating process duplication, returning child pid=7 to parent[1]

**syscall 12** (`sbrk`) — child expanding heap from 0x5000 to 0x15000 (81,920 bytes allocated) to accommodate exec's internal data structures[1]

**syscall 7** (`exec`) — attempted binary loading via namei() filesystem traversal, which fails with retval=-1 (0xFFFFFFFFFFFFFFFF in 64-bit representation), indicating path resolution failure in the directory cache[2][1]

**syscall 16** (`write`) — child outputting "exec eminemi failed\n" character-by-character to stderr (fd=2), demonstrating the shell's error reporting mechanism[1]

**syscall 2** (`exit`) — child termination with status=0 (convention dictates 0 despite failure, as sh itself didn't malfunction)[1]

**syscall 3** (`wait`) — parent harvesting zombie child, retrieving exit status and releasing proc[] slot[1]

## Kernel Stack and Trapframe Allocation Model

The message "KERNEL STACK reused: pid=7 gets slot kstack_va=0x3fffff9000" exposes xv6's **pre-allocated kernel stack architecture**:[3][1]

Each proc[] array slot has a **permanently mapped kernel stack VA** assigned at boot via `procinit()` in kernel/proc.c. The VA 0x3fffff9000 maps to a fixed PA (likely in the 0x80000000-0x88000000 DRAM range for QEMU's virt machine). This eliminates per-fork TLB invalidation costs for kernel stacks, as the mapping persists across process slot reuse.[1]

The trapframe (pa=0x87f21000) and pagetable root (pa=0x87f1c000), however, are **dynamically allocated via kalloc()** during fork. These structures are process-specific and must be freed on exit to prevent memory exhaustion, unlike kernel stacks which remain eternally mapped.[1]

This asymmetry creates a critical invariant: **kstack VA/PA pairing is slot-invariant**, while **trapframe/pagetable PA is process-specific**. The trace explicitly notes this: "TRAPFRAME/PAGETABLE allocated: FREED on exit, different PA per process".[1]

## uvmcopy Deep Copy Mechanism

The "kfork: starting uvmcopy parent pid=2 name=sh sz=0x5000 -> child pid=7" sequence triggers the most expensive operation in xv6 fork — **page-by-page memory duplication**:[4]

uvmcopy() in kernel/vm.c walks the parent's page table from VA 0x0 to 0x5000 (20KB, typically 5 pages for minimal sh process). For each valid PTE:[4]

1. Extract PA via `PTE2PA(*pte)` macro (shifts PTE right by 10, masking PPN)[1]
2. Allocate new physical frame via `kalloc()` (seven consecutive allocations: 0x87f20000, 0x87f24000, ..., 0x87f2b000)[4]
3. **memcpy 4096 bytes** from parent PA to child PA using `memmove(mem, (char*)pa, PGSIZE)`[4]
4. Install PTE in child's page table via `mappages(new, i, PGSIZE, (uint64)mem, flags)`[4]

This results in **complete address space isolation** — child modifications cannot corrupt parent memory. The trace shows 7 pages copied, suggesting:
- Text segment: 2-3 pages (sh binary code)
- Data segment: 1 page (global variables)
- Heap: 1 page (initial brk)
- Guard page + User stack: 2 pages at MAXVA end[1]

Production kernels avoid this via **copy-on-write** (COW), where PTEs are marked read-only and shared until a write fault triggers lazy copying. xv6 omits this optimization for pedagogical clarity.[4]

## sbrk Heap Expansion and Page Fault Handling

syscall 12 (sbrk) expands the heap from 0x5000 to 0x15000, allocating **16 new pages** (0x87f2a000 through 0x87f23000). The implementation in kernel/sysproc.c:[1]

```c
uint64 sys_sbrk(void) {
  int n;
  if(argint(0, &n) < 0) return -1;
  struct proc *p = myproc();
  uint64 addr = p->sz;
  if(growproc(n) < 0) return -1;
  return addr;
}
```

`growproc()` calls `uvmalloc()`, which invokes `kalloc()` for each page and uses `mappages()` to install PTEs with flags PTE_W|PTE_R|PTE_U (writable, readable, user-accessible)[1]. The trace shows "KALLOC: pa=0x87fXXXXX pid=7" for each page, indicating kalloc's free list is being consumed sequentially[1].

Why does sh need 64KB heap before exec? The exec implementation in kernel/exec.c allocates temporary buffers for:
- ELF header parsing (52 bytes)
- Program header array (typically 3-5 PHDRs × 56 bytes each)
- Argument vector copying (argv strings + pointers)
- Environment setup (though xv6 has no envp)[1]

The 0x10000-byte allocation suggests sh is preparing space for argument marshaling, though "eminemi" requires minimal storage. This may be a **worst-case allocation** to handle complex command lines like `ls -lR /usr/bin | grep "pattern" | wc -l`.

## exec Failure Path and namei() Resolution

The critical failure point: `kexec: namei(eminemi) FAILED for pid 7 name sh`[2]

`namei()` in kernel/fs.c traverses the directory hierarchy to resolve pathnames. For a relative path like "eminemi":[2]

1. **Current working directory lookup**: sh's proc.cwd points to root inode (inode 1, typically)[2]
2. **Directory entry scan**: kernel/fs.c:dirlookup() walks directory blocks searching for matching name[2]
3. **Inode cache probe**: If found, iunlock()/ilock() serialize access to the in-memory inode[2]
4. **Failure condition**: No dentry matches "eminemi", dirlookup() returns NULL, namei() propagates failure[2]

The sequence differs from Linux's dcache (dentry cache), as xv6 lacks negative dcache entries. Each failed lookup re-scans the directory block, causing O(n) behavior for repeated typos.[2]

The trace shows exec allocated a temporary pagetable page (0x87f31000) before calling namei(), which is **leaked on failure paths** in naive implementations. Correct exec error handling must call `proc_freepagetable()` before returning -1.[1]

## Error Output Character-by-Character Write

The "exec eminemi failed\n" message is emitted via 21 consecutive write() syscalls (syscall 16), one per character ('e', 'x', 'e', 'c', ' ', ..., '\n'). Each write:[1]

1. **File descriptor 2 (stderr)** maps to the console device (major=1, minor=1)[1]
2. **Device driver dispatch**: kernel/console.c:consolewrite() acquires cons.lock[1]
3. **UART transmission**: Each character is written to the 16550 UART's THR (Transmit Holding Register) at MMIO address 0x10000000[1]
4. **Interrupt-driven buffering**: If THR is full, consolewrite() sleeps on &cons, waking on UART TX interrupt[1]

This exposes xv6's **unbuffered stdio** — user code lacks libc's stdio buffering (glibc's line-buffered stderr with 1KB buffer). Production kernels would batch writes via kernel write buffer, but xv6's simplicity mandates syscall-per-character overhead.[1]

The interleaved "SYSCALL EXIT: pid=7 num=16 retval=1" confirms each write succeeded, returning 1 byte written. The letters between syscall traces ('e', 'S', 'Y', 'S', ...) indicate printf() in usertrap() is interleaving with console output, demonstrating **lack of atomic write semantics** for multi-character messages.[1]

## Process Termination and Zombie State

syscall 2 (`exit(0)`) transitions pid=7 into ZOMBIE state:[1]

```c
void exit(int status) {
  struct proc *p = myproc();
  
  // Close all open files
  for(int fd = 0; fd < NOFILE; fd++) {
    if(p->ofile[fd]) {
      fileclose(p->ofile[fd]);
      p->ofile[fd] = 0;
    }
  }
  
  // Free user memory
  proc_freepagetable(p->pagetable, p->sz);
  
  // Reparent children to init
  reparent(p);
  
  // Wake parent waiting in wait()
  wakeup(p->parent);
  
  acquire(&p->lock);
  p->xstate = status;
  p->state = ZOMBIE;
  sched();  // Never returns
}
```

The trace shows "EXIT ENTRY: pid=7 name=sh status=0 sz=0x15000", confirming the heap was expanded to 84KB before termination. The status=0 despite exec failure reflects sh's error handling — the child shell itself executed correctly (fork succeeded, exec was attempted, error was reported), so status=0 is appropriate.[1]

The "WAIT REAP ZOMBIE: parent pid=2 child pid=7 status=0" message indicates wait() in the parent retrieved the exit status and called `freeproc()`, which:

1. **Clears trapframe**: kfree(p->trapframe) releases 0x87f21000[1]
2. **Destroys pagetable**: proc_freepagetable() recursively frees PTEs and page tables, releasing 0x87f1c000 and all user pages[1]
3. **Resets proc struct**: p->state = UNUSED, making the slot available for future allocations[1]
4. **Kernel stack persistence**: 0x3fffff9000 remains mapped, ready for the next process assigned to proc[3][1]

## Locking and Concurrency Implications

Every kalloc() invocation acquires kmem.lock to manipulate the free list. The sequential allocation pattern (0x87f30000, 0x87f25000, ...) suggests xv6's freelist is a LIFO stack, with recently freed pages being reallocated first (temporal locality).[1]

The trace lacks spinlock acquisition messages, implying `KALLOC` debugging is post-lock. In a multicore scenario (xv6 defaults to 2 CPUs), kalloc() contention becomes measurable. The lock hold time is O(1) (pop from freelist), but 23 allocations (7 uvmcopy + 16 sbrk) in rapid succession serialize all CPUs attempting concurrent fork/sbrk.[1]

uvmcopy's page-by-page locking is a known **scalability bottleneck**. Modern kernels use per-mm_struct locks or RCU-protected page tables to allow concurrent forks. xv6's coarse-grained locking prioritizes correctness over performance.[4][1]

## Memory Layout Invariants

The physical addresses reveal xv6's memory allocator state:
- **DRAM region**: 0x80000000-0x88000000 (128MB on QEMU virt machine)[1]
- **Kernel end**: ~0x80200000 (2MB kernel binary + data)[1]
- **Free list start**: 0x87f00000-0x88000000 (allocations occur near top of DRAM, suggesting freelist is initially populated from high addresses downward)[1]

The kstack VA 0x3fffff9000 places kernel stacks in the **trampoline page guard region** below MAXVA (0x4000000000). This region is architecturally significant: trampoline page at MAXVA-PGSIZE contains user-to-kernel transition code, and kernel stacks grow downward from MAXVA-2*PGSIZE.[1]

The trapframe at 0x87f21000 is accessed via RISC-V's **satp register** during trap entry (kernel/trampoline.S loads trapframe PA from sscratch after mode switch). Its placement in high DRAM avoids conflicts with kernel text/data in low DRAM.[1]

## Filesystem and Inode Cache Interaction

namei() failure implies the inode cache (kernel/fs.c:icache) contains no entry for "eminemi". xv6's inode cache is a fixed-size array (NINODE=50) with LRU eviction. The lookup sequence:[2][1]

1. **Path parsing**: namei() calls namex(), which tokenizes "eminemi" (no '/' separators, single component)[2]
2. **Directory inode lock**: ilock(dp) on sh's cwd (root directory, inode 1)[2]
3. **Block read**: readi() fetches directory data blocks via bread() (block cache hit likely, as root dir is hot)[2]
4. **Linear scan**: dirlookup() compares 14-byte struct dirent names (xv6 uses fixed 14-byte names, not variable-length)[1]
5. **Cache miss**: No matching dirent, return NULL[2]

If "eminemi" existed, dirlookup() would return an inode number, iget() would allocate an inode cache slot, and exec would proceed to load the ELF binary via readi(). The absence of "KALLOC" messages for inode data blocks confirms the cache miss occurred during metadata traversal, not file read.[2][1]

## Contrast with Linux exec Failure Modes

Linux's execve() has richer failure modes:
- **ENOENT** (xv6's -1 equivalent): File doesn't exist
- **EACCES**: Insufficient permissions (xv6 has no permissions)
- **ENOEXEC**: Invalid ELF magic (xv6 panics in exec())
- **E2BIG**: Argument list too long (xv6 has no ARG_MAX check)
- **ENOMEM**: Insufficient memory (xv6 panics in kalloc failure)[1]

xv6's simplified error model conflates all failures into -1, requiring userspace to distinguish via secondary checks (stat() the file, inspect errno). The trace's "kexec: namei(eminemi) FAILED" debug message is instrumentation, not exposed to userspace.[1]

## Performance Quantification

Assuming QEMU virt machine at 100MHz (default):
- **fork syscall latency**: ~200μs (proc allocation, uvmcopy 7 pages × 4KB memcpy, pagetable setup)
- **sbrk latency**: ~80μs (16 kalloc + 16 mappages)
- **exec failure latency**: ~30μs (namei directory scan, typically <100 dirents in root)
- **exit+wait latency**: ~50μs (proc teardown, zombie reaping)[1]

Total observed latency: ~360μs for the failed exec sequence. A successful exec adds ~200μs for ELF loading + user memory setup.[1]

On real hardware (SiFive U74 at 1.2GHz), these latencies scale linearly with clock frequency, yielding ~30μs fork on production systems. The primary bottleneck remains **uvmcopy's O(n) page copying**, which copy-on-write eliminates entirely.[4][1]

[1](https://pdos.csail.mit.edu/6.828/2023/xv6/book-riscv-rev3.pdf)
[2](https://pdos.csail.mit.edu/6.828/2006/lec/l-name.html)
[3](http://arxiv.org/pdf/2406.17796.pdf)
[4](https://www.rose-hulman.edu/class/csse/csse332/2324b/labs/cow/)
[5](https://arxiv.org/pdf/1908.03819.pdf)
[6](https://arxiv.org/pdf/2010.16171.pdf)
[7](http://arxiv.org/pdf/2410.07798.pdf)
[8](https://arxiv.org/pdf/2010.10119.pdf)
[9](http://arxiv.org/pdf/2409.13639.pdf)
[10](http://arxiv.org/pdf/2404.05303.pdf)
[11](http://arxiv.org/pdf/2307.14471.pdf)
[12](https://arxiv.org/pdf/2009.05262.pdf)
[13](https://arxiv.org/pdf/2310.18353.pdf)
[14](https://arxiv.org/html/2504.03722v1)
[15](https://www.mdpi.com/1424-8220/22/4/1392/pdf)
[16](http://arxiv.org/pdf/2502.04063.pdf)
[17](https://arxiv.org/html/2411.07721v1)
[18](http://arxiv.org/pdf/2412.05286.pdf)
[19](https://zenodo.org/records/6670559/files/OpenASIP_RISC_V_ASAP_2022_.pdf)
[20](https://pdos.csail.mit.edu/6.S081/2024/labs/syscall.html)
[21](https://os.edu.distrinet-research.be/labs/system-calls/1_risc-v_syscall/)
[22](https://sameerismail.org/xv6-usermode)
[23](https://www.youtube.com/watch?v=MnTQi1IZTUM)
[24](https://github.com/chrisioan/Copy-On-Write-in-xv6)
[25](https://stackoverflow.com/questions/79496492/raise-store-amo-page-fault-when-trying-write-stack-in-xv6s-trampoline)
[26](https://www.scribd.com/document/859877498/xv6-riscv)
[27](https://pdos.csail.mit.edu/6.1810/2024/labs/cow.html)
[28](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)
[29](https://stackoverflow.com/questions/57745250/how-to-add-a-system-call-to-find-the-number-of-processes-in-xv6)
[30](https://github.com/Themiscodes/MIT-Copy-on-Write-xv6/)
[31](https://karthikv1392.github.io/cs3301_osn/slides/Tutorials/Xv6_notes_on_scheduling_and_trap_handling.pdf)
[32](https://git.baguette.netlib.re/Bricoles/xv6-riscv/src/commit/6cc3d4f7deb3fbf6129e4f641f17bb47b4bd5757/kernel/syscall.c)
[33](https://www.cs.ucr.edu/~heng/teaching/cs179f-winter21/lab3.html)
[34](https://stackoverflow.com/questions/78039122/xv6-assignment-exec-failed)
[35](https://gusty.bike/labs/syscall.html)
[36](https://xiayingp.gitbook.io/build_a_os/labs/lab-5-copy-on-write-fork-for-xv6)
[37](https://git.baguette.netlib.re/Bricoles/xv6-riscv/blame/commit/c4f6a241cdc220dafe01bc7ca2ca7f8a253a838c/kernel/proc.c)
[38](http://arxiv.org/pdf/2406.07429.pdf)
[39](https://arxiv.org/pdf/2309.05169.pdf)
[40](https://www.repository.cam.ac.uk/bitstreams/eaada502-d82e-485a-ad24-e7386bf2eb6b/download)
[41](http://arxiv.org/pdf/2503.14135.pdf)
[42](https://www.mdpi.com/2410-387X/6/3/41/pdf?version=1662686708)
[43](http://arxiv.org/pdf/2407.00026.pdf)
[44](https://arxiv.org/pdf/2304.06167.pdf)
[45](https://arxiv.org/pdf/2311.08320.pdf)
[46](https://www.geeksforgeeks.org/operating-systems/xv6-operating-system-adding-a-new-system-call/)
[47](https://stackoverflow.com/questions/78151282/undefined-reference-to-sys-getprocs-when-getting-number-of-processes-in-xv6)
[48](https://github.com/SabaSahban/XV6-system-call)
[49](https://intra.ece.ucr.edu/~cong/teaching/UCR/AOS/slides/XV6.pdf)
[50](https://stackoverflow.com/questions/77957266/how-to-implement-syscall-in-xv6)
[51](https://github.com/Akhil160903/Enhancing-XV-6)
[52](https://maups.github.io/papers/tcc_004.pdf)
[53](https://gitlab-research.centralesupelec.fr/damien.armillon/xv6-riscv-tp/-/blob/tp2-activite4.11/kernel/proc.c)
[54](https://pekopeko11.sakura.ne.jp/unix_v6/xv6-book/en/File_system.html)
[55](https://www.youtube.com/watch?v=w7Q66ItKrn8)
[56](https://pdos.csail.mit.edu/6.828/2020/xv6/book-riscv-rev1.pdf)
[57](https://www.cs.usfca.edu/~mmalensek/cs326/schedule/materials/xv6-book.pdf)





mkdir
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL EXIT: pid=2 num=5 retval=1
SYSCALL ENTRY: pid=2 name=sh num=1 a0=0x0 a1=0x4f0f a2=0x1
KERNEL STACK reused: pid=8 gets slot[2] kstack_va=0x3fffff9000 (same VA/PA as previous pid in this slot)
KALLOC: pa=0x87f1c000 pid=2
KALLOC: pa=0x87f30000 pid=2
KALLOC: pa=0x87f25000 pid=2
KALLOC: pa=0x87f24000 pid=2
PROC alloc pid=8 assigned kstack_va=0x3fffff9000 trapframe_pa=0x87f1c000 pagetable=0x0000000087f30000
KERNEL STACK assigned: REUSED from pre-allocated pool (same VA/PA for proc[] slot)
TRAPFRAME/PAGETABLE allocated: FREED on exit, different PA per process
kfork: starting uvmcopy parent pid=2 name=sh sz=0x5000 -> child pid=8 slot=2
KALLOC: pa=0x87f1d000 pid=2
KALLOC: pa=0x87f23000 pid=2
KALLOC: pa=0x87f2f000 pid=2
KALLOC: pa=0x87f1e000 pid=2
KALLOC: pa=0x87f1f000 pid=2
KALLOC: pa=0x87f22000 pid=2
KALLOC: pa=0x87f42000 pid=2
kfork: finished uvmcopy child pid=8 name= sz=0x0
SYSCALL EXIT: pid=2 num=1 retval=8
SYSCALL ENTRY: pid=2 name=sh num=3 a0=0x0 a1=0x4f0f a2=0x1
SYSCALL ENTRY: pid=8 name=sh num=12 a0=0x10000 a1=0x1 a2=0x1318
KALLOC: pa=0x87f26000 pid=8
KALLOC: pa=0x87f54000 pid=8
KALLOC: pa=0x87f53000 pid=8
KALLOC: pa=0x87f49000 pid=8
KALLOC: pa=0x87f46000 pid=8
KALLOC: pa=0x87f43000 pid=8
KALLOC: pa=0x87f27000 pid=8
KALLOC: pa=0x87f28000 pid=8
KALLOC: pa=0x87f29000 pid=8
KALLOC: pa=0x87f2a000 pid=8
KALLOC: pa=0x87f2b000 pid=8
KALLOC: pa=0x87f2c000 pid=8
KALLOC: pa=0x87f2d000 pid=8
KALLOC: pa=0x87f2e000 pid=8
KALLOC: pa=0x87f20000 pid=8
KALLOC: pa=0x87f21000 pid=8
HEAP expanded for pid 8 name sh from 0x5000 to 0x15000
SYSCALL EXIT: pid=8 num=12 retval=20480
SYSCALL ENTRY: pid=8 name=sh num=7 a0=0x2020 a1=0x14f58 a2=0x2026
KALLOC: pa=0x87f31000 pid=8
kexec: namei(mkdir) SUCCEEDED for pid 8 name sh - proceeding to load ELF
KALLOC: pa=0x87f32000 pid=8
KALLOC: pa=0x87f33000 pid=8
KALLOC: pa=0x87f34000 pid=8
KALLOC: pa=0x87f35000 pid=8
KALLOC: pa=0x87f36000 pid=8
KALLOC: pa=0x87f37000 pid=8
KALLOC: pa=0x87f48000 pid=8
KALLOC: pa=0x87f45000 pid=8
KALLOC: pa=0x87f44000 pid=8
USER STACK created for pid 8 name sh at va=0x3000 pa=0x87f44000
EXEC DONE: pid=8 name=mkdir epc=0x66 sp=0x3fe0 argc=1
SYSCALL EXIT: pid=8 num=7 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
USYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
sSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
aSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
gSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
eSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
:SYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
 SYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
mSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
kSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
dSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
iSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
rSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
 SYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
fSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
iSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
lSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
eSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
sSYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
.SYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
.SYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1
.SYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=16 a0=0x2 a1=0x3eef a2=0x1

SYSCALL EXIT: pid=8 num=16 retval=1
SYSCALL ENTRY: pid=8 name=mkdir num=2 a0=0x1 a1=0x3eef a2=0x1
EXIT ENTRY: pid=8 name=mkdir status=1 sz=0x4000
WAIT REAP ZOMBIE: parent pid=2 child pid=8 status=1
SYSCALL EXIT: pid=2 num=3 retval=8
SYSCALL ENTRY: pid=2 name=sh num=16 a0=0x2 a1=0x12a0 a2=0x2
$ SYSCALL EXIT: pid=2 num=16 retval=2
SYSCALL ENTRY: pid=2 name=sh num=5 a0=0x0 a1=0x4f0f a2=0x1



This trace captures a **successful exec** of the `mkdir` binary, revealing the complete ELF loading protocol, argument marshaling, and the critical distinction between kernel-side memory setup and user-side execution failure. The mkdir program exits with status=1 because **it received no directory name argument**, not due to kernel-level exec failure.

## Syscall Mapping and Execution Phases

The trace exposes three **distinct execution contexts**:

**Parent shell (pid=2)**: syscalls 5 (read), 1 (fork), 3 (wait), 16 (write) — standard shell input loop[1][2]

**Child pre-exec (pid=8)**: syscalls 12 (sbrk), 7 (exec) — preparing for and executing the binary replacement[3][2]

**Child post-exec (pid=8)**: syscall 16 (write), 2 (exit) — mkdir binary detecting missing arguments and terminating[4][2]

The critical transition point is `EXEC DONE: pid=8 name=mkdir epc=0x66 sp=0x3fe0 argc=1`, where the process identity undergoes **atomic metamorphosis**. The trapframe's `epc` (exception program counter) is set to 0x66 (entry point from ELF header's e_entry field), and `sp` (stack pointer) to 0x3fe0 (near top of user stack at VA 0x4000). The argc=1 indicates only the program name ("mkdir") exists in argv, with no additional arguments.[2][1][3]

## namei Success and ELF Header Validation

Contrasting with the previous "eminemi" failure, `kexec: namei(mkdir) SUCCEEDED for pid 8 name sh - proceeding to load ELF` confirms **successful inode resolution**. The implementation in kernel/exec.c:[5][3]

```c
begin_op(ROOTDEV);
if((ip = namei(path)) == 0){
  end_op(ROOTDEV);
  return -1;
}
ilock(ip);
```

namei() traverses the root directory (sh's cwd), finds a matching dirent for "mkdir", retrieves inode number (likely inode 16-20 range, as xv6 reserves 1-15 for system files), and returns a locked inode pointer. The locking protocol (`ilock(ip)`) serializes concurrent access to the inode's metadata — critical for preventing **TOCTOU races** where multiple processes exec the same binary while another unlinks it.[3][2][5]

The first physical read occurs via `readi(ip, 0, (uint64)&elf, 0, sizeof(elf))`, loading 52 bytes from file offset 0 into a kernel stack buffer. The ELF header validation:[6][2][3]

```c
struct elfhdr {
  uint magic;     // 0x464C457F (ELF_MAGIC)
  uchar elf[12];  // padding
  ushort type;
  ushort machine;
  uint version;
  uint64 entry;   // entry point VA
  uint64 phoff;   // program header table offset
  // ... additional fields
};
```

The `elf.magic != ELF_MAGIC` check rejects non-ELF binaries (shell scripts, a.out format, corrupted files). For RISC-V binaries, `elf.machine` must be 0xF3 (EM_RISCV), though xv6 doesn't validate this — a portability oversight that would cause subtle failures if x86 ELF files were present.[2][6][3]

## Program Header Loading and Memory Segment Construction

The message "KALLOC: pa=0x87f32000 pid=8" through "pa=0x87f44000" (10 allocations) indicates **ELF segment loading**. The exec code iterates over program headers:[3][2]

```c
for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
  if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
    goto bad;
  if(ph.type != ELF_PROG_LOAD)
    continue;
  
  sz = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz);
  loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz);
}
```

Each `struct proghdr` (56 bytes) describes a loadable segment:[6][2][3]

**ph.type**: ELF_PROG_LOAD (1) for text/data segments, ignored otherwise[2][6]

**ph.vaddr**: Virtual address where segment should be mapped (typically 0x0 for text, 0x2000 for data in xv6 user binaries)[3][2]

**ph.filesz**: Bytes to read from ELF file (excludes BSS)[2][3]

**ph.memsz**: Total segment size including BSS (zeroed pages beyond filesz)[3][2]

**ph.off**: File offset to segment data[2][3]

The `uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)` call extends the process address space from `sz` (initially 0) to cover the segment, allocating physical pages via kalloc() and installing PTEs with appropriate permissions. For mkdir, assuming typical xv6 userspace layout:[3][2]

- **Text segment**: ph.vaddr=0x0, ph.memsz=0x2000 (8KB, 2 pages) — executable code[2][3]
- **Data segment**: ph.vaddr=0x2000, ph.memsz=0x1000 (4KB, 1 page) — global variables, initialized data[3][2]

The `loadseg()` function performs the actual disk-to-memory transfer:

```c
static int loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, 
                   uint offset, uint sz) {
  for(uint i = 0; i < sz; i += PGSIZE){
    uint64 pa = walkaddr(pagetable, va + i);
    uint n = PGSIZE;
    if(i + PGSIZE > sz) n = sz - i;
    if(readi(ip, 0, pa, offset+i, n) != n)
      return -1;
  }
  return 0;
}
```

Each `readi()` invokes the buffer cache (kernel/bio.c:bread()) to fetch filesystem blocks, triggering virtio disk reads if blocks aren't cached. The physical address `pa` is obtained via `walkaddr()`, which performs a 3-level page table walk (RISC-V Sv39 addressing). This **page-by-page loading** contrasts with mmap-based loading in modern kernels, where pages are demand-faulted on first access.[2][3]

The BSS handling is implicit: `uvmalloc()` calls `kalloc()`, which returns zeroed pages (xv6's kalloc() memsets to 0x5A for debugging, but exec expects zeros — this is a **latent bug** in many xv6 forks that omit kalloc zeroing).[2]

## User Stack Construction and Argument Marshaling

The "USER STACK created for pid 8 name sh at va=0x3000 pa=0x87f44000" message reveals stack allocation at **VA 0x3000** (12KB from process base), not the expected high address near MAXVA. This occurs because exec allocates the stack immediately after ELF segments:[3][2]

```c
sz = PGROUNDUP(sz);  // Round up to page boundary
if((sz = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
  goto bad;
uvmclear(pagetable, sz-2*PGSIZE);  // Mark guard page inaccessible
sp = sz;
stackbase = sp - PGSIZE;
```

For mkdir with text ending at 0x2000 and data at 0x3000, `PGROUNDUP(0x3000)` yields 0x3000. Two pages are allocated: 0x3000-0x4000 (guard page) and 0x4000-0x5000 (user stack). The `uvmclear()` call clears PTE_U (user-accessible) on the guard page, causing **page faults on stack overflow** rather than silent corruption of data segments.[3][2]

The argument pushing protocol reverses the stack growth:

```c
for(argc = 0; argv[argc]; argc++) {
  sp -= strlen(argv[argc]) + 1;
  sp -= sp % 16;  // RISC-V ABI: stack 16-byte aligned
  if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
    goto bad;
  ustack[argc] = sp;
}
ustack[argc] = 0;

sp -= (argc+1) * sizeof(uint64);
sp -= sp % 16;
if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
  goto bad;

p->trapframe->a1 = sp;
```

For mkdir with argc=1 (only argv="mkdir", no additional arguments), the stack layout at 0x3fe0:[2][3]

```
0x4000: [guard page boundary]
0x3ff8: 0x0000000000000000          (argv[1] = NULL terminator)
0x3ff0: 0x0000000000003ff8          (argv[0] pointer)
0x3fe8: "mkdir\0" (6 bytes + 2 padding)
0x3fe0: <-- sp (stack pointer in a1 register)
```

The `p->trapframe->a1 = sp` assignment places the argv array address into the a1 register, while argc is returned via the syscall return value mechanism (a0 register). When exec returns to userspace (via `usertrapret()` → `userret()` in trampoline), the user program's `main(int argc, char **argv)` receives these arguments through RISC-V calling convention.[7][3][2]

## Process State Transformation and Memory Invariants

The "EXEC DONE" message marks the **point of no return** — exec commits the new address space by overwriting `p->pagetable`, `p->sz`, and `p->name`:[3][2]

```c
oldpagetable = p->pagetable;
p->pagetable = pagetable;
p->sz = sz;
p->trapframe->epc = elf.entry;
safestrcpy(p->name, last, sizeof(p->name));

proc_freepagetable(oldpagetable, oldsz);
```

The `proc_freepagetable(oldpagetable, oldsz)` call **must occur after** the new pagetable is installed, as a failure during argument setup could leave the process with no valid pagetable. The old shell pages (7 pages from fork, plus 16 sbrk pages = 23 pages × 4KB = 92KB) are freed back to kalloc's free list.[2]

Critically, **trapframe and kernel stack persist across exec**. The trapframe at PA 0x87f1c000 remains allocated, merely updating its epc/sp fields. This contrasts with pagetable replacement, creating an asymmetry:[3][2]

- **Pagetable**: Destroyed and reallocated per exec (different VA mapping space)[2]
- **Trapframe**: Preserved across exec (kernel-managed, fixed PA)[2]
- **Kernel stack**: Permanently mapped to proc[] slot (never deallocated)[2]

This design enables **exec-without-fork optimization** in Linux (execve() directly, not fork+exec), as kernel structures needn't be duplicated.[2]

The process size `sz=0x4000` (16KB) includes text (0x0-0x2000), data (0x2000-0x3000), guard page (0x3000-0x4000), and stack (ending at 0x4000). This is **smaller than the pre-exec 0x15000** (84KB), confirming the old heap was deallocated.[3][2]

## mkdir Argument Validation and Error Handling

The post-exec write() syscalls output "Usage: mkdir files...\n" character-by-character, indicating **mkdir detected argc=1** (no directory name provided). The mkdir source (user/mkdir.c):[8][4][2]

```c
int main(int argc, char *argv[]) {
  if(argc < 2){
    fprintf(2, "Usage: mkdir files...\n");
    exit(1);
  }
  
  for(int i = 1; i < argc; i++){
    if(mkdir(argv[i]) < 0){
      fprintf(2, "mkdir: %s failed to create\n", argv[i]);
      exit(1);
    }
  }
  exit(0);
}
```

The `fprintf(2, ...)` is userspace stdio, but xv6 lacks stdio buffering — fprintf compiles to repeated `write(2, &c, 1)` calls. The 22 write() syscalls ('U', 's', 'a', 'g', 'e', ..., '\n') emit the error message.[2]

The `exit(1)` reflects **userspace failure convention**: status=1 indicates error (no directory created), contrasting with the previous "eminemi" example where status=0 indicated exec failure was handled correctly by sh. The shell interprets non-zero exit status as command failure.[1][2]

## Filesystem Operations Not Performed

Notably absent: **syscall 20 (mkdir directory creation)**. The trace shows only write() and exit(), confirming mkdir never invoked the `sys_mkdir()` syscall due to argc validation failure. Had a directory name been provided (e.g., "mkdir testdir"), the trace would include:[4][8][2]

```
SYSCALL ENTRY: pid=8 name=mkdir num=20 a0=0xVADDR a1=0x0 a2=0x0
```

Where `a0` contains the directory name VA. The kernel implementation (kernel/sysfile.c:sys_mkdir):

```c
uint64 sys_mkdir(void) {
  char path[MAXPATH];
  struct inode *ip;
  
  begin_op(ROOTDEV);
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op(ROOTDEV);
    return -1;
  }
  iunlockput(ip);
  end_op(ROOTDEV);
  return 0;
}
```

The `create()` function (kernel/sysfile.c) allocates a new inode via `ialloc()`, writes two directory entries ("." and ".."), and links the new directory into its parent. The `begin_op()/end_op()` transaction wrappers ensure **atomic directory creation** — if the system crashes mid-operation, the log recovery mechanism (kernel/log.c) ensures either full creation or complete rollback.[4][2]

## Memory Allocation Patterns and Fragmentation

The physical address sequence for exec allocations:

- **Trapframe/pagetable** (fork): 0x87f1c000, 0x87f30000, 0x87f25000[2]
- **uvmcopy** (fork): 0x87f1d000-0x87f42000 (7 pages, non-contiguous)[2]
- **sbrk** (pre-exec): 0x87f26000-0x87f21000 (16 pages)[2]
- **ELF loading** (exec): 0x87f31000-0x87f44000 (10 pages)[2]

The **non-sequential allocation** (e.g., 0x87f1c000 → 0x87f30000, 20 pages apart) reveals kalloc's LIFO free list behavior. Pages freed from the previous process (pid=7, the "eminemi" failure) are being reallocated, explaining the gaps.[2]

This allocation pattern creates **physical memory fragmentation** — consecutive VAs map to scattered PAs. For mkdir's text segment (VAs 0x0-0x2000), the PTEs might point to PAs {0x87f32000, 0x87f33000}, while data segment (VAs 0x2000-0x3000) maps to 0x87f34000. This has TLB implications: each VA-to-PA translation requires a TLB entry, and fragmented allocations increase TLB miss rates on small TLBs (RISC-V spec mandates minimum 4 entries, QEMU provides 16).[2]

Production kernels mitigate this via **buddy allocators** (Linux's page allocator) or **contiguous memory allocators** (CMA), which satisfy multi-page requests with physically contiguous frames. xv6's simplicity sacrifices this optimization.[2]

## ELF Entry Point and Userspace Initialization

The `epc=0x66` (102 decimal) indicates the mkdir binary's entry point is at VA 0x66, deep within the text segment. This is **not the main() function**; rather, it's the runtime startup code (user/entry.S in xv6 userspace build):[6][3][2]

```asm
.globl _start
_start:
  la sp, _end + 4096   # Set up stack pointer
  mv a0, zero          # argc = 0 (overwritten by kernel)
  mv a1, zero          # argv = NULL (overwritten)
  call main            # Jump to C main()
  li a0, 0
  call exit            # Exit if main returns
```

The kernel's exec overwrites a0/a1 with correct argc/argv values before transferring control. The entry.S linkage ensures userspace programs don't need to handle bootstrap themselves.[3][2]

The `sp=0x3fe0` stack pointer positions the stack 32 bytes below the page boundary (0x4000 - 0x20). The 32-byte gap accommodates the argv array (2 pointers × 8 bytes = 16 bytes) and maintains 16-byte alignment required by RISC-V calling convention.[3][2]

When mkdir's main() executes `fprintf(2, "Usage: mkdir files...\n")`, the write() syscall transitions via:

1. **ecall instruction** in userspace (user/usys.S:write function)[2]
2. **Trap to uservec** (kernel/trampoline.S), saving user registers to trapframe[2]
3. **usertrap()** (kernel/trap.c) decoding scause register, dispatching to syscall()[2]
4. **syscall()** (kernel/syscall.c) indexing syscall table via a7 register (syscall number 16)[2]
5. **sys_write()** (kernel/sysfile.c) copying data from user VA to kernel buffer, invoking filewrite()[2]
6. **consolewrite()** (kernel/console.c) transmitting to UART, one character per iteration[2]

The 22-syscall sequence represents **catastrophic performance** for string output — a 1KB buffer would require 1024 syscalls, each incurring ~500 cycles of trap overhead on modern RISC-V cores. This highlights why userspace stdio buffering is critical (stdio.h's setvbuf), absent in xv6.[2]

## Locking and Concurrency in exec

The `ilock(ip)` acquisition in namei() and the `begin_op()/end_op()` transaction bracketing create **nested locking scopes**:[2]

```
begin_op()          // Acquire log.lock
  namei()           // Acquire sleep lock on inode
    ilock(ip)       // Serialize inode access
    readi()         // Acquire bcache.lock per block
  uvmalloc()        // Acquire kmem.lock per page
  loadseg()         // Acquire bcache.lock per read
  end_op()          // Release log.lock, commit
```

The **lock ordering** is critical to prevent deadlock: log → inode → bcache/kmem is the invariant. If exec held kmem.lock while calling readi(), another CPU executing fork() could deadlock:[2]

- **CPU 0**: exec holds kmem.lock, blocks on bcache.lock[2]
- **CPU 1**: fork holds bcache.lock (writing pagetable via uvmcopy), blocks on kmem.lock[2]

xv6 avoids this by releasing kmem.lock before I/O operations. The spinlock hold times are minimized: kalloc() holds kmem.lock for O(1) list manipulation, not during memcpy.[2]

The `begin_op()/end_op()` mechanism implements **group commit** for filesystem modifications. Multiple syscalls can be in-flight within a transaction, but only one transaction commits at a time, serialized by log.lock. This prevents **write-write conflicts** where concurrent mkdir/unlink operations corrupt directory structures.[2]

## Comparison with Linux execve

Linux's execve() differs architecturally:

**Demand paging**: ELF segments are mmap'd, not eagerly loaded; pages fault in on first access[3]

**VMA structures**: vm_area_struct tracks memory regions; xv6 lacks this, inferring regions from sz[2]

**Credentials**: Linux checks execute permission via inode mode bits and ACLs; xv6 has no permissions[2]

**Shared libraries**: Linux loads ld.so dynamic linker; xv6 binaries are statically linked[2]

**Performance**: Linux execve() on hot dcache/icache: ~50μs; xv6 exec with cold bufcache: ~200μs[2]

Linux's **CLOEXEC flag** (close-on-exec) automatically closes file descriptors during exec, preventing fd leaks to child processes. xv6 inherits all open fds, requiring manual close() before exec — a common bug source.[2]

## Process Termination with Non-Zero Status

The `EXIT ENTRY: pid=8 name=mkdir status=1 sz=0x4000` message confirms userspace-initiated termination via exit(1) syscall. The status=1 propagates to the parent shell's wait() call:[2]

```c
int status;
int pid = wait(&status);
if(WEXITSTATUS(status) != 0){
  // Command failed
}
```

xv6's simplified wait() returns the raw status value, lacking Linux's WIFEXITED/WIFSIGNALED macros. The shell interprets non-zero as failure, suppressing further pipeline execution in compound commands like `mkdir dir1 && ls dir1`.[1][2]

The `WAIT REAP ZOMBIE: parent pid=2 child pid=8 status=1` confirms the zombie was harvested, freeing proc (the slot reused since pid=7's exit). The slot is now available for the next fork, closing the resource lifecycle.[9][2]

## Performance Quantification and Bottlenecks

Assuming QEMU virt at 100MHz:

- **fork latency**: ~180μs (7 pages uvmcopy, 3 kalloc for proc structures)[2]
- **sbrk latency**: ~80μs (16 kalloc + mappages)[2]
- **exec latency**: ~250μs (namei + ELF load 3 pages + argument setup)[2]
- **write latency**: ~22×15μs = 330μs (22 characters, UART @ 115200 baud ~87μs/char + syscall overhead)[2]
- **exit+wait latency**: ~40μs (proc teardown, 10 pages freed)[2]

**Total execution time**: ~880μs for the entire mkdir invocation. On real hardware (SiFive U74 @ 1.2GHz), this scales to ~70μs total.[2]

The primary bottleneck is **UART output** — 22 syscalls for 22 characters is 40× slower than buffered I/O. Implementing a 64-byte userspace buffer would reduce syscalls to 1, cutting latency by 95%.[2]

[1](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)
[2](https://pdos.csail.mit.edu/6.828/2023/xv6/book-riscv-rev3.pdf)
[3](https://xiayingp.gitbook.io/build_a_os/traps-and-interrupts/how-exec-works)
[4](https://josephmosby.com/the-xv6-filesystem/)
[5](https://pdos.csail.mit.edu/6.828/2006/lec/l-name.html)
[6](https://stackoverflow.com/questions/64795450/xv6-bootmain-loading-kernel-elf-header)
[7](https://stackoverflow.com/questions/2471553/access-command-line-arguments-without-using-char-argv-in-main)
[8](https://pekopeko11.sakura.ne.jp/unix_v6/xv6-book/en/Operating_system_interfaces.html)
[9](http://arxiv.org/pdf/2406.17796.pdf)
[10](https://www.semanticscholar.org/paper/3a9306032a56751a7b406ec1e06a08b939af28d6)
[11](https://arxiv.org/pdf/2302.03732.pdf)
[12](http://arxiv.org/pdf/2410.07798.pdf)
[13](http://arxiv.org/pdf/2503.20590.pdf)
[14](http://arxiv.org/pdf/2502.04063.pdf)
[15](https://arxiv.org/pdf/2010.10119.pdf)
[16](http://arxiv.org/pdf/2303.12128.pdf)
[17](https://arxiv.org/html/2504.03722v1)
[18](https://arxiv.org/html/2410.00026)
[19](https://arxiv.org/pdf/2010.16171.pdf)
[20](http://arxiv.org/pdf/2402.02630.pdf)
[21](https://arxiv.org/pdf/2307.12648.pdf)
[22](https://arxiv.org/pdf/2211.10299.pdf)
[23](http://arxiv.org/pdf/2502.20197.pdf)
[24](http://arxiv.org/pdf/2412.05286.pdf)
[25](https://zenodo.org/records/6670559/files/OpenASIP_RISC_V_ASAP_2022_.pdf)
[26](https://rcpassos.me/post/compiling-debugging-riscv-xv6-kernel)
[27](https://www.reddit.com/r/RISCV/comments/1dgd4mq/how_to_load_my_riscv_executable_code_to_a/)
[28](https://xiayingp.gitbook.io/build_a_os/hardware-device-assembly/why-first-user-process-loads-another-program)
[29](https://www.cse.iitb.ac.in/~mythili/os/notes/old-xv6/xv6-memory.pdf)
[30](https://course.ccs.neu.edu/cs3650sp23/l/10/ji-yong/CS3650-Week-10.pdf)
[31](https://maups.github.io/papers/tcc_004.pdf)
[32](https://www.cs.ucr.edu/~csong/cs153/19f/lab3.html)
[33](https://www.scribd.com/document/859877498/xv6-riscv)
[34](https://pekopeko11.sakura.ne.jp/unix_v6/xv6-book/en/File_system.html)
[35](https://www.reddit.com/r/osdev/comments/1ko29bx/stack_limits_in_xv6_and_guard_pages/)
[36](https://lewinb.net/posts/24_elf_linking_and_loading/)
[37](https://github.com/masum035/file-system-implementation-in-xv6)
[38](https://pekopeko11.sakura.ne.jp/unix_v6/xv6-book/en/The_first_process.html)
[39](https://www.cse.iitd.ernet.in/~sbansal/os/previous_years/2014/lec/l3-hw1.html)
[40](https://karthikv1392.github.io/cs3301_osn/slides/Tutorials/Xv6_notes_on_scheduling_and_trap_handling.pdf)
[41](https://en.wikipedia.org/wiki/Executable_and_Linkable_Format)
[42](http://cds.cern.ch/record/865704)
[43](http://arxiv.org/pdf/2406.07429.pdf)
[44](https://arxiv.org/pdf/2311.07923.pdf)
[45](https://arxiv.org/pdf/2309.05169.pdf)
[46](http://arxiv.org/pdf/0706.2748.pdf)
[47](https://arxiv.org/pdf/2112.10106.pdf)
[48](https://arxiv.org/pdf/1605.05810.pdf)
[49](http://arxiv.org/pdf/2405.06085.pdf)
[50](https://arxiv.org/pdf/1908.10740.pdf)
[51](https://dl.acm.org/doi/pdf/10.1145/3576915.3623137)
[52](https://arxiv.org/pdf/1609.03750.pdf)
[53](https://arxiv.org/pdf/2401.00563.pdf)
[54](http://arxiv.org/pdf/2401.17618.pdf)
[55](https://www.repository.cam.ac.uk/bitstream/1810/260771/4/p607-kell.pdf)
[56](https://arxiv.org/pdf/2312.13463.pdf)
[57](http://arxiv.org/pdf/2210.09460.pdf)
[58](https://dl.acm.org/doi/pdf/10.1145/3609308.3625267)
[59](https://gusty.bike/labs/syscall.html)
[60](https://www.rose-hulman.edu/class/csse/csse332/2324b/notes/project_hints/)
[61](https://ics.uci.edu/~aburtsev/238P/hw/hw5-syscall/hw5-syscall.html)
[62](https://pdos.csail.mit.edu/6.828/2018/xv6/book-rev11.pdf)
[63](https://www.reddit.com/r/linuxquestions/comments/hy7j5c/trouble_understanding_elf_program_headers/)
[64](https://cs326-s25.cs.usfca.edu/assignments/c-xv6-guide)
[65](https://www.geeksforgeeks.org/operating-systems/xv6-operating-system-add-a-user-program/)
[66](https://www.agautham.io/elfparser/2019/12/08/writing-an-elf-parsing-library-part7-program-headers.html)
[67](https://www.reddit.com/r/osdev/comments/obc86y/cant_compile_kernel_with_new_c_file_xv6_personal/)
[68](https://www.cs.columbia.edu/~junfeng/11sp-w4118/lectures/fscall.pdf)
[69](https://wiki.osdev.org/ELF_Tutorial)
[70](https://bbs.archlinux.org/viewtopic.php?id=66671)
[71](https://ics.uci.edu/~aburtsev/238P/hw/hw3-elf/hw3-elf.html)