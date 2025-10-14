#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "trace.h"

// simple per-cpu ring buffer
static struct trace_event trace_buf[NCPU][TRACE_SIZE];
static uint32 trace_head[NCPU];

void
trace_emit(uint16 type, uint64 d0, uint64 d1, uint64 d2, uint64 d3, uint64 d4, uint64 d5)
{
  int cpu = cpuid();
  uint32 idx = trace_head[cpu]++ & (TRACE_SIZE - 1);
  struct trace_event *e = &trace_buf[cpu][idx];
  e->tsc = r_time();
  e->cpu = cpu;
  e->type = type;
  struct proc *p = myproc();
  e->pid = p ? p->pid : 0;
  if(p)
    memmove(e->name, p->name, sizeof(e->name));
  else
    e->name[0] = '\0';
  e->data[0] = d0; e->data[1] = d1; e->data[2] = d2;
  e->data[3] = d3; e->data[4] = d4; e->data[5] = d5;
}

// syscall to read events from a given CPU's ring buffer
// Kernel-side syscall implementation with signature expected by syscall table
uint64
sys_trace_read(void)
{
  uint64 buf;
  int n;
  int cpu;
  if(argaddr(0, &buf) < 0)
    return -1;
  if(argint(1, &n) < 0)
    return -1;
  if(argint(2, &cpu) < 0)
    return -1;

  if(cpu < 0 || cpu >= NCPU)
    return -1;
  if(n <= 0 || n > TRACE_SIZE)
    return -1;

  uint32 head = trace_head[cpu];
  uint32 avail = head < TRACE_SIZE ? head : TRACE_SIZE;
  uint32 tocopy = n;
  if(tocopy > avail)
    tocopy = avail;

  uint32 start = (head - tocopy) & (TRACE_SIZE - 1);
  uint32 first = TRACE_SIZE - start;

  if(first >= tocopy) {
    char *src = (char*)&trace_buf[cpu][start];
    uint64 len = tocopy * sizeof(struct trace_event);
    if(copyout(myproc()->pagetable, buf, src, len) < 0)
      return -1;
  } else {
    char *src1 = (char*)&trace_buf[cpu][start];
    uint64 len1 = first * sizeof(struct trace_event);
    if(copyout(myproc()->pagetable, buf, src1, len1) < 0)
      return -1;
    char *src2 = (char*)&trace_buf[cpu][0];
    uint64 len2 = (tocopy - first) * sizeof(struct trace_event);
    if(copyout(myproc()->pagetable, buf + len1, src2, len2) < 0)
      return -1;
  }
  return tocopy;
}
