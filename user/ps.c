#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

char *states[] = {
  [0] "UNUSED",
  [1] "USED", 
  [2] "SLEEPING",
  [3] "RUNNABLE",
  [4] "RUNNING",
  [5] "ZOMBIE"
};

void print_header() {
  printf("PID  PPID NAME             STATE     SIZE(KB) CHAN     \n");
  printf("---- ---- ---------------- --------- -------- ---------\n");
}

void print_process(struct proc_info *info) {
  printf("%-4d %-4d %-16s %-9s %-8lu 0x%-7lx\n", 
         info->pid, 
         info->parent_pid, 
         info->name, 
         states[info->state],
         info->sz / 1024,  // Convert bytes to KB
         info->chan);
}

void clear_screen() {
  // Simple screen clear - print newlines to push content up
  for(int i = 0; i < 30; i++) {
    printf("\n");
  }
}

int main(int argc, char *argv[]) {
  struct proc_info info;
  int refresh_time = 2;  // Default refresh every 2 seconds
  int show_all = 0;      // By default, don't show UNUSED processes
  
  // Parse command line arguments
  for(int i = 1; i < argc; i++) {
    if(strcmp(argv[i], "-a") == 0) {
      show_all = 1;
    } else if(strcmp(argv[i], "-h") == 0) {
      printf("Usage: ps [-a] [-t seconds]\n");
      printf("  -a: show all processes including UNUSED\n");
      printf("  -t: set refresh time in seconds (default: 2)\n");
      printf("  Press Ctrl+C to exit\n");
      exit(0);
    } else if(strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      refresh_time = atoi(argv[i + 1]);
      if(refresh_time <= 0) refresh_time = 1;
      i++; // Skip the next argument since we used it
    }
  }
  
  printf("Real-time Process Table Viewer (refresh every %d seconds)\n", refresh_time);
  printf("Press Ctrl+C to exit\n\n");
  
  while(1) {
    int process_count = 0;
    
    clear_screen();
    printf("=== XV6 Process Table ===\n");
    print_header();
    
    // Iterate through all possible PIDs
    for(int pid = 1; pid <= 64; pid++) {
      if(getprocinfo(pid, &info) == 0) {
        // Process exists
        if(show_all || info.state != 0) {  // 0 = UNUSED
          print_process(&info);
          process_count++;
        }
      }
    }
    
    printf("\nTotal active processes: %d\n", process_count);
    printf("Refreshing in %d seconds... (Ctrl+C to exit)\n", refresh_time);
    
    // Sleep for the specified time
    pause(refresh_time * 100);  // pause() takes ticks, assuming ~100 ticks per second
  }
  
  exit(0);
}