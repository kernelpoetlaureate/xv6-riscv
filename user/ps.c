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

// Helper function to print a string with padding
void print_padded(char *str, int width, int left_align) {
  int len = strlen(str);
  if (left_align) {
    printf("%s", str);
    for (int i = len; i < width; i++) printf(" ");
  } else {
    for (int i = len; i < width; i++) printf(" ");
    printf("%s", str);
  }
}

// Helper function to print an integer with padding
void print_int_padded(int val, int width, int left_align) {
  char buf[16];
  int len = 0;
  
  // Convert int to string manually
  if (val == 0) {
    buf[len++] = '0';
  } else {
    if (val < 0) {
      buf[len++] = '-';
      val = -val;
    }
    int digits[16];
    int digit_count = 0;
    while (val > 0) {
      digits[digit_count++] = val % 10;
      val /= 10;
    }
    for (int i = digit_count - 1; i >= 0; i--) {
      buf[len++] = '0' + digits[i];
    }
  }
  buf[len] = '\0';
  
  print_padded(buf, width, left_align);
}

// Helper function to print uint64 with padding
void print_uint64_padded(uint64 val, int width, int left_align) {
  char buf[32];
  int len = 0;
  
  if (val == 0) {
    buf[len++] = '0';
  } else {
    uint64 digits[32];
    int digit_count = 0;
    while (val > 0) {
      digits[digit_count++] = val % 10;
      val /= 10;
    }
    for (int i = digit_count - 1; i >= 0; i--) {
      buf[len++] = '0' + digits[i];
    }
  }
  buf[len] = '\0';
  
  print_padded(buf, width, left_align);
}

// Helper function to print hex with padding
void print_hex_padded(uint64 val, int width) {
  char buf[32];
  char digits[] = "0123456789abcdef";
  int len = 0;
  
  if (val == 0) {
    buf[len++] = '0';
  } else {
    uint64 temp = val;
    while (temp > 0) {
      buf[len++] = digits[temp % 16];
      temp /= 16;
    }
    // Reverse the string
    for (int i = 0; i < len / 2; i++) {
      char temp = buf[i];
      buf[i] = buf[len - 1 - i];
      buf[len - 1 - i] = temp;
    }
  }
  buf[len] = '\0';
  
  printf("0x");
  print_padded(buf, width, 0);  // Right-aligned for hex
}

void print_header() {
  printf("PID  PPID NAME             STATE     SIZE(KB) CHAN     \n");
  printf("---- ---- ---------------- --------- -------- ---------\n");
}

void print_process(struct proc_info *info) {
  // PID - left aligned, width 4
  print_int_padded(info->pid, 4, 1);
  printf(" ");
  
  // PPID - left aligned, width 4  
  print_int_padded(info->parent_pid, 4, 1);
  printf(" ");
  
  // NAME - left aligned, width 16
  print_padded(info->name, 16, 1);
  printf(" ");
  
  // STATE - left aligned, width 9
  print_padded(states[info->state], 9, 1);
  printf(" ");
  
  // SIZE(KB) - right aligned, width 8
  print_uint64_padded(info->sz / 1024, 8, 0);
  printf(" ");
  
  // CHAN - hex with 0x prefix, width 7 for hex part
  print_hex_padded(info->chan, 7);
  printf("\n");
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
      printf("  Program auto-exits after 5 seconds\n");
      exit(0);
    } else if(strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
      refresh_time = atoi(argv[i + 1]);
      if(refresh_time <= 0) refresh_time = 1;
      i++; // Skip the next argument since we used it
    }
  }
  
  // Set auto-exit timeout to 5 seconds
  const int TIMEOUT_SECONDS = 5;
  int total_ticks = TIMEOUT_SECONDS * 100;  // Assuming 100 ticks per second
  int elapsed_ticks = 0;
  
  printf("Real-time Process Table Viewer (refresh every %d sec, auto-exits in %d sec)\n", 
         refresh_time, TIMEOUT_SECONDS);
  printf("Press Ctrl+C to exit early\n\n");
  
  while(elapsed_ticks < total_ticks) {
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
    printf("Refreshing in %d seconds... (auto-exits in %d sec)\n", 
           refresh_time, (TIMEOUT_SECONDS - elapsed_ticks / 100));
    
    // Sleep for the specified time
    pause(refresh_time * 100);  // pause() takes ticks, assuming ~100 ticks per second
    elapsed_ticks += refresh_time * 100;
  }
  
  printf("\nAuto-exiting after %d seconds...\n", TIMEOUT_SECONDS);
  
  exit(0);
}