#include "types.h"
#include "user.h"
#include "procstat.h"

#define MAX_PROCS 64

void sort_by_cpu(struct procstat *arr, int n) {
  for(int i = 0; i < n-1; i++){
    for(int j = 0; j < n-i-1; j++){
      if(arr[j].total_ticks < arr[j+1].total_ticks){
        struct procstat tmp = arr[j];
        arr[j] = arr[j+1];
        arr[j+1] = tmp;
      }
    }
  }
}

int find_prev(struct procstat *prev, int n, int pid){
  for(int i = 0; i < n; i++) if(prev[i].pid == pid) return i;
  return -1;
}

int main(int argc, char **argv){
  struct procstat *procs = malloc(MAX_PROCS * sizeof(*procs));
  struct procstat *prev = malloc(MAX_PROCS * sizeof(*prev));
  if(!procs || !prev){
    printf("htop: malloc failed\n");
    return 1;
  }
  int nprev = 0;

  for(;;){
    int n = procstat((uint64)procs, MAX_PROCS);
    if(n <= 0){
      printf("procstat failed or no processes\n");
      sleep(100);
      continue;
    }

    sort_by_cpu(procs, n);

    // Clear screen and home cursor
    printf("\x1b[2J\x1b[H");
  printf("PID NAME STATE RAM_KB CPU%% IO_R IO_W\n");

    for(int i = 0; i < n; i++){
      // Skip empty / UNUSED entries (pid==0 and no counters)
      if(procs[i].pid == 0 && procs[i].total_ticks == 0 && procs[i].sz == 0 && procs[i].io_reads == 0 && procs[i].io_writes == 0)
        continue;
      int pidx = find_prev(prev, nprev, procs[i].pid);
      int cpu_pct = 0;
      if(pidx >= 0){
        uint64 delta = procs[i].total_ticks - prev[pidx].total_ticks;
        cpu_pct = (int)delta; // interval is 100 ticks (1s)
      }
      printf("%d %s %d %d %d%% %d %d\n",
        procs[i].pid,
        procs[i].name,
        procs[i].state,
        (int)(procs[i].sz / 1024),
        cpu_pct,
        (int)procs[i].io_reads,
        (int)procs[i].io_writes);
    }

  // copy to prev
  nprev = n;
  for(int i = 0; i < n; i++) prev[i] = procs[i];

    sleep(100);
  }
  free(procs);
  free(prev);
  return 0;
}
