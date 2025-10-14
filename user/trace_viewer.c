#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/syscall.h"

// syscall prototype generated in user/usys.S
extern int trace_read(uint64, int, int);

struct trace_event_u {
  uint64 tsc;
  uint16 cpu;
  uint16 type;
  uint32 pid;
  char name[16];
  uint64 data[6];
};

int
main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  struct trace_event_u *buf = malloc(1024 * sizeof(struct trace_event_u));
  if(!buf) {
    printf("trace_viewer: malloc failed\n");
    exit(1);
  }
  int n;
  while((n = trace_read((uint64)buf, 1024, 0)) > 0) {
    for(int i = 0; i < n; i++) {
      struct trace_event_u *e = &buf[i];
      printf("[%ld] cpu=%d pid=%d type=%d name=%s\n", e->tsc, e->cpu, e->pid, e->type, e->name);
    }
    sleep(1);
  }
  free(buf);
  exit(0);
}
