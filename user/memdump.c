// memdump: userland helper to print a small memory region.
//
// Usage:
//   memdump <addr> [length]      - dump memory at specific address
//   memdump -stack [-PID N]      - dump stack pages for current process or PID N
//   memdump -heap [-PID N]       - dump heap-like pages for current process or PID N
//
// If <addr> is below the kernel base (KERNBASE) it is treated as a
// user virtual address. If <addr> is at or above KERNBASE (a kernel virtual 
// address), memdump uses the `kread` syscall to safely ask the kernel to copy 
// up to 4096 bytes from kernel virtual memory into the user buffer and then 
// prints those bytes.
//
// For user virtual addresses, the program attempts to directly read the memory,
// but handles cases where pages may not be mapped by gracefully reporting
// access errors.
//
// Security/safety notes:
//  - kread is intentionally limited to a page (4096 bytes) to limit the
//    amount of kernel memory a user program can read in one syscall.
//  - kread copies from the kernel virtual address space (not raw physical
//    memory). To inspect physical pages that don't have a kernel virtual
//    mapping, use the pageinfo_phys/pageinfo_va helpers to discover mappings
//    then request mapped VAs.
//
// Simple memory region dumper for xv6 userland
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "procstat.h"
#include "pageinfo.h"

// Kernel virtual base: user addresses should be below this.
// KERNBASE is 0x80000000 in xv6/riscv.
#define KERNBASE 0x80000000UL

// parse address string: supports 0x... hex and decimal
unsigned long
parse_addr(const char *s)
{
  unsigned long val = 0;
  if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')){
    s += 2;
    while(*s){
      char c = *s++;
      val <<= 4;
      if(c >= '0' && c <= '9') val += c - '0';
      else if(c >= 'a' && c <= 'f') val += 10 + c - 'a';
      else if(c >= 'A' && c <= 'F') val += 10 + c - 'A';
      else break;
    }
  } else {
    while(*s >= '0' && *s <= '9'){
      val = val * 10 + (*s - '0');
      s++;
    }
  }
  return val;
}

// Check if a page is mapped by consulting the current process's page map
int
is_page_mapped(unsigned long addr)
{
  int pid = getpid();
  struct page_info pages[256];  // Reasonable buffer size
  int got = get_pagemap(pid, (uint64)pages, 256);
  
  if(got <= 0) return 0;
  
  unsigned long page_addr = addr & ~0xFFF;  // Round down to page boundary
  
  for(int i = 0; i < got; i++) {
    if((pages[i].va & ~0xFFF) == page_addr) {
      return 1;  // Page is mapped
    }
  }
  return 0;  // Page not found in mapping
}

// Print a memory region. 'base' is the address to display as the
// start of the region (for kernel reads this will be the kernel
// virtual address requested), 'start' points to the buffer holding
// the bytes to print (may be in user space), and 'length' is how
// many bytes to print.
void
dump_memory_range(unsigned long base, char *start, int length, const char *region_type)
{
  // print header with region type and address
  printf("\n=== %s at ", region_type);
  printf("%p", (void*)base);
  printf(" ===\n");

  // helper: convert nibble (0..15) to hex char
  char hexchar[16];
  for(int i = 0; i < 16; i++) hexchar[i] = "0123456789ABCDEF"[i];

  for(int i = 0; i < length; i++) {
    if(i % 16 == 0) {
      // print newline then 4-digit hex offset (zero-padded).
      printf("\n");
      int off = i & 0xFFFF;
      // four hex digits: print high nibble to low
      printf("%c", hexchar[(off >> 12) & 0xF]);
      printf("%c", hexchar[(off >> 8) & 0xF]);
      printf("%c", hexchar[(off >> 4) & 0xF]);
      printf("%c", hexchar[(off >> 0) & 0xF]);
      printf(": ");
    }
    unsigned char b = (unsigned char)start[i];
    printf("%c", hexchar[(b >> 4) & 0xF]);
    printf("%c", hexchar[b & 0xF]);
    printf(" ");
  }
  printf("\n");
}

// Safe version that handles potentially unmapped user memory
void
dump_user_memory_range(unsigned long base, int length, const char *region_type)
{
  // Check if we can safely access this memory by checking if pages are mapped
  unsigned long start_page = base & ~0xFFF;
  unsigned long end_page = (base + length - 1) & ~0xFFF;
  
  // Check if all required pages are mapped
  for(unsigned long page = start_page; page <= end_page; page += 4096) {
    if(!is_page_mapped(page)) {
      printf("Memory at ");
      printf("%p", (void*)base);
      printf(":\n\nError: Page at 0x%lx is not mapped in current process\n", page);
      return;
    }
  }
  
  // If we get here, all pages should be safe to access
  dump_memory_range(base, (char*)base, length, region_type);
}

int
main(int argc, char **argv)
{
  // Modes: -stack, -heap, -all, -pid
  int mode_stack = 0;
  int mode_heap = 0;
  int mode_all = 0;
  int mode_pid = 0;
  int target_pid = 0;

  // If invoked with a single flag like -stack or -heap, use those modes.
  // Accept forms:
  //   memdump -stack [-PID N]
  //   memdump -heap  [-PID N]
  // Otherwise fallback to original: memdump <addr> [length]

  if(argc >= 2) {
    if(strcmp(argv[1], "-stack") == 0) {
      mode_stack = 1;
    } else if(strcmp(argv[1], "-heap") == 0) {
      mode_heap = 1;
    } else if(strcmp(argv[1], "-all") == 0) {
      mode_all = 1;
      mode_stack = 1;
      mode_heap = 1;
    } else if(strcmp(argv[1], "-pid") == 0 && argc >= 3) {
      mode_pid = 1;
      target_pid = atoi(argv[2]);
    }
  }

  // parse -PID if present
  for(int i = 1; i < argc; i++){
    if(strcmp(argv[i], "-PID") == 0 && i+1 < argc){
      mode_pid = 1;
      target_pid = atoi(argv[i+1]);
      i++;
    }
  }

  // Helper: get procstat list
  // procstat fills an array of struct procstat (user/procstat.h)
  if(mode_stack || mode_heap || mode_all){
    int MAX_PROCS = 64;
    struct procstat *procs = malloc(MAX_PROCS * sizeof(*procs));
    if(!procs){
      printf("memdump: failed to allocate procstat buffer\n");
      exit(1);
    }
    int n = procstat((uint64)procs, MAX_PROCS);
    if(n <= 0){
      printf("memdump: procstat failed or returned no processes\n");
      free(procs);
      exit(1);
    }

    // For each process (or the target PID), call get_pagemap to inspect pages
    for(int i = 0; i < n; i++){
      if(procs[i].pid <= 0) continue;
      if(mode_pid && procs[i].pid != target_pid) continue;

      int pid = procs[i].pid;
  // allocate buffer for page_info entries
      struct page_info *pages = malloc(1024 * sizeof(*pages));
      if(!pages) continue;
      int got = get_pagemap(pid, (uint64)pages, 1024);
      if(got <= 0){ free(pages); continue; }

      // Determine stack pages: userstack pages are at high addresses near sz
      // In xv6 exec, stack is allocated at sz .. sz + USERSTACK*PGSIZE (USERSTACK small)
      // We'll treat pages with VA >= (procs[i].sz - (USERSTACK*PGSIZE)) as stack.
      uint64 USERSTACK_PAGES = 1; // match kernel param.h USERSTACK
      uint64 PGSIZE64 = 4096ULL;
      uint64 stack_threshold = 0;
      if(procs[i].sz >= USERSTACK_PAGES * PGSIZE64)
        stack_threshold = procs[i].sz - USERSTACK_PAGES * PGSIZE64;

      if(mode_stack){
        // Print header for this pid
        printf("PID %d (%s) stack pages:\n", pid, procs[i].name);
        for(int j = 0; j < got; j++){
          uint64 va = pages[j].va;
          if(va >= stack_threshold && va < procs[i].sz){
            // Only dump if we're reading our own process memory
            if(pid == getpid()) {
              dump_memory_range((unsigned long)va, (char*)va, (int)PGSIZE64, "STACK MEMORY");
            } else {
              // For other processes, we can't directly access their memory
              printf("Cannot access memory of PID %d from current process\n", pid);
            }
          }
        }
      }

      if(mode_heap){
        // Heuristic: treat writable pages with VA < sz and not in stack region as heap
        printf("PID %d (%s) heap-like pages:\n", pid, procs[i].name);
        for(int j = 0; j < got; j++){
          uint64 va = pages[j].va;
          // skip stack pages
          if(va >= stack_threshold && va < procs[i].sz) continue;
          // consider pages below sz
          if(va < procs[i].sz){
            // Only dump if we're reading our own process memory
            if(pid == getpid()) {
              dump_memory_range((unsigned long)va, (char*)va, (int)PGSIZE64, "HEAP MEMORY");
            } else {
              // For other processes, we can't directly access their memory
              printf("Cannot access memory of PID %d from current process\n", pid);
            }
          }
        }
      }

      free(pages);
    }

    free(procs);
    exit(0);
  }

  // original behaviour: treat argv[1] as address
  if(argc < 2) {
    printf("Usage:\n");
    printf("  memdump <addr> [length]     - dump memory at specific address\n");
    printf("  memdump -stack [-PID N]     - dump all stack pages\n");
    printf("  memdump -heap [-PID N]      - dump all heap pages\n");
    printf("  memdump -all [-PID N]       - dump all memory regions\n");
    printf("  memdump -pid N              - list all memory regions for PID N\n");
    exit(1);
  }

  unsigned long addr = parse_addr(argv[1]);
  int length = 64;
  if(argc >= 3) length = atoi(argv[2]);

  if(addr >= KERNBASE){
    /* Read kernel memory via syscall kread into a local buffer */
    if(length > 4096) length = 4096; // match kernel limit
    char *buf = malloc(length);
    if(!buf){
      printf("memdump: failed to allocate buffer\n");
      exit(1);
    }
    if(kread(addr, length, buf) < 0){
      printf("memdump: kread failed for 0x%lx\n", addr);
      free(buf);
      exit(1);
    }
    dump_memory_range(addr, buf, length, "KERNEL MEMORY");
    free(buf);
    exit(0);
  }
  
  // For user virtual addresses, try to access them directly
  // Note: This may fail if the memory is not mapped or accessible
  printf("Attempting to read user virtual address 0x%lx\n", addr);
  dump_user_memory_range(addr, length, "USER MEMORY");
  exit(0);
}
