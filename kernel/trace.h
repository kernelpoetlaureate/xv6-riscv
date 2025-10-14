// Simple kernel trace ring buffer used by plan1 instrumentation
#ifndef TRACE_H
#define TRACE_H

#include "types.h"

#define TRACE_SIZE 16384

enum trace_type {
  TRACE_FORK = 1,
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
  uint64 tsc;
  uint16 cpu;
  uint16 type;
  uint32 pid;
  char name[16];
  uint64 data[6];
};

void trace_emit(uint16 type, uint64 d0, uint64 d1, uint64 d2, uint64 d3, uint64 d4, uint64 d5);

#endif // TRACE_H
