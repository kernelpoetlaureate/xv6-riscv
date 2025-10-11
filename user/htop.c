#/*********************************************
# htop (simple process monitor)
#
# Notes:
# - This program is non-interactive: it prints process info in a loop
#   and does not read stdin. That means typing keys like 'q' or Ctrl-C
#   while htop runs will generally have no effect here.
# - To run it and keep a shell prompt, start it from the xv6 shell with
#     htop &
#   The trailing '&' is a shell feature (implemented in `user/sh.c`):
#   the shell forks and runs the command in a background child. See the
#   BACK case in `runcmd()` and the parser token for '&' in `parsecmd`.
# - To stop a backgrounded htop, use the shell's `kill` command with the
#   process ID, for example:
#     kill <pid>
#   You can find the PID with Ctrl-P (prints process list) or `ps`.
#*********************************************/

#include "types.h"
#include "user.h"
#include "procstat.h"
#include "pageinfo.h"

#define MAX_PROCS 64

// Simple helpers: xv6 user printf doesn't support width/flags
static int
ustrlen(const char *s){
  int i=0; while(s && s[i]) i++; return i;
}

// print string s truncated to at most 'max' chars (no null-termination needed)
static void
print_trunc(const char *s, int max){
  int i;
  for(i = 0; i < max && s[i]; i++)
    printf("%c", s[i]);
}

// print s in a field of width 'w'. If left_align non-zero, left-align; else right-align.
static void
print_field_str(const char *s, int w, int left_align){
  int len = ustrlen(s);
  if(len >= w){
    print_trunc(s, w);
    return;
  }
  int pad = w - len;
  if(left_align){
    printf("%s", s);
    for(int i = 0; i < pad; i++) printf(" ");
  } else {
    for(int i = 0; i < pad; i++) printf(" ");
    printf("%s", s);
  }
}

// convert integer (non-negative) to decimal string in buf; returns length
static int
itoa(int v, char *buf, int bufsz){
  if(bufsz <= 0) return 0;
  if(v == 0){ if(bufsz > 1){ buf[0] = '0'; buf[1] = '\0'; return 1; } buf[0] = '\0'; return 0; }
  int neg = 0;
  unsigned int x = v;
  if(v < 0){ neg = 1; x = (unsigned int)(-v); }
  char tmp[32]; int ti = 0;
  while(x && ti < (int)sizeof(tmp)-1){ tmp[ti++] = '0' + (x % 10); x /= 10; }
  if(neg) tmp[ti++] = '-';
  int len = 0;
  // reverse into buf
  for(int j = ti-1; j >= 0 && len < bufsz-1; j--){ buf[len++] = tmp[j]; }
  buf[len] = '\0';
  return len;
}

static void
print_field_int(int v, int w){
  char buf[32];
  int len = itoa(v, buf, sizeof(buf));
  (void)len; // silence unused-variable if any toolchain warns
  // right align numeric fields
  print_field_str(buf, w, 0);
}

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
  // Column widths: PID(5) NAME(16) STATE(6) VIRT_KB(8) RSS_KB(8) RSS%(6) CPU%(5) IO_R(6) IO_W(6)
    print_field_str("PID", 5, 1); printf(" ");
    print_field_str("NAME", 16, 1); printf(" ");
    print_field_str("STATE", 6, 1); printf(" ");
  print_field_str("VIRT_KB", 8, 0); printf(" ");
  print_field_str("RSS_KB", 8, 0); printf(" ");
  print_field_str("RSS%", 6, 0); printf(" ");
    print_field_str("CPU%", 5, 0); printf(" ");
    print_field_str("IO_R", 6, 0); printf(" ");
    print_field_str("IO_W", 6, 0); printf("\n");

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
      // Truncate name to fit column and print fields
      char namebuf[17];
      for(int k = 0; k < 16; k++) namebuf[k] = procs[i].name[k];
      namebuf[16] = '\0';
      print_field_int(procs[i].pid, 5); printf(" ");
      print_field_str(namebuf, 16, 1); printf(" ");
      // STATE currently numeric; print right-aligned in 6 columns
      print_field_int(procs[i].state, 6); printf(" ");
      int virt_kb = (int)(procs[i].sz / 1024);
      int rss_kb = 0;
      int rss_pct = 0;
      int rss = getrss(procs[i].pid);
      if(rss >= 0){
        rss_kb = rss / 1024;
        if(procs[i].sz > 0) rss_pct = (int)((rss * 100) / procs[i].sz);
      }
      print_field_int(virt_kb, 8); printf(" ");
      print_field_int(rss_kb, 8); printf(" ");
      print_field_int(rss_pct, 6); printf(" ");
      // CPU% with a trailing percent sign
      char cpubuf[16]; int cpulen = itoa(cpu_pct, cpubuf, sizeof(cpubuf));
      if(cpulen + 1 <= 5){ // leave room for '%'
        // right align into 5 columns
        for(int s = 0; s < 5 - (cpulen+1); s++) printf(" ");
        printf("%s%%", cpubuf);
      } else {
        print_trunc(cpubuf, 5-1); printf("%%");
      }
      printf(" ");
      print_field_int((int)procs[i].io_reads, 6); printf(" ");
      print_field_int((int)procs[i].io_writes, 6); printf("\n");
    }

  // copy to prev
  nprev = n;
  for(int i = 0; i < n; i++) prev[i] = procs[i];

    sleep(100);
  }
  
  // If htop was called with a PID, show its pagemap once before exiting (non-interactive)
  if(argc >= 2){
    int pid = atoi(argv[1]);
    struct page_info *pages = malloc(4096 * sizeof(*pages));
    if(pages){
      int got = get_pagemap(pid, (uint64)pages, 4096);
      if(got < 0) {
        printf("get_pagemap failed for pid %d\n", pid);
      } else {
        printf("PID %d Page Map (%d pages):\n", pid, got);
        printf("VA               PA               FLAGS REFS TYPE OWNER TAG\n");

        // pageinfo_user matches the user-side layout used by memdump_phys/dumppi
        struct pageinfo_user {
          unsigned char type;
          int owner_pid;
          uint64 mapped_va;
          char tag[16];
          uint64 alloc_tick;
          uint64 ref;
        } pi;

        for(int i = 0; i < got; i++){
          // Try to fetch kernel pageinfo for this virtual address. If it fails,
          // fallback to printing VA/PA/flags from get_pagemap.
          int r = pageinfo_va((uint64)&pi, pages[i].va);
          if(r < 0){
            printf("0x%016lx  0x%016lx  %04x %4d - - -\n",
                   pages[i].va, pages[i].pa, pages[i].flags, pages[i].refcount);
          } else {
            // Print tag as a short string (may not be NUL-terminated)
            char tagbuf[17];
            for(int t = 0; t < 16; t++) tagbuf[t] = pi.tag[t] ? pi.tag[t] : ' ';
            tagbuf[16] = '\0';
            printf("0x%016lx  0x%016lx  %04x %4d  %2d   %3d %s\n",
                   pages[i].va, pages[i].pa, pages[i].flags, pages[i].refcount,
                   (int)pi.type, pi.owner_pid, tagbuf);
          }
        }
      }
      free(pages);
    }
  }
  free(procs);
  free(prev);
  return 0;
}
