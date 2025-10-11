// User-side procstat definition; mirrors kernel/procstat.h
#ifndef _USER_PROCSTAT_H_
#define _USER_PROCSTAT_H_

#include "types.h"

struct procstat {
  int pid;
  int state;
  uint64 sz;
  uint64 total_ticks;
  uint64 ctime;
  uint64 etime;
  uint64 io_reads;
  uint64 io_writes;
  char name[16];
};

#endif
