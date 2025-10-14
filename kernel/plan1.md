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