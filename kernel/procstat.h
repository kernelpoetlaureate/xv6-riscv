// Userspace-visible process statistics structure
#ifndef _PROCSTAT_H_
#define _PROCSTAT_H_

#include "types.h"

struct procstat {
  int pid;
  int state;
  uint64 sz;          // Memory size (bytes)
  uint64 total_ticks; // CPU time (ticks)
  uint64 ctime;       // Creation time (ticks)
  uint64 etime;       // End time (ticks)
  uint64 io_reads;
  uint64 io_writes;
  char name[16];
};

#endif // _PROCSTAT_H_
