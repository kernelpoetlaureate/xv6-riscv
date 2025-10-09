#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PAGEINFO_TAGLEN 16
struct pageinfo_user {
  unsigned char type;
  int owner_pid;
  uint64 mapped_va;
  char tag[PAGEINFO_TAGLEN];
  uint64 alloc_tick;
  uint64 ref;
};

static const char *type_name(unsigned char t) {
  switch(t) {
    case 0: return "FREE";
    case 1: return "KERNEL";
    case 2: return "USER";
    case 3: return "BCACHE";
    case 4: return "PAGETBL";
    case 0xff: return "UNKNOWN";
    default: return "?";
  }
}

// Print a memdump-style page dump so the existing annotate script can parse it.
// base is the kernel virtual address corresponding to this physical page.
void dump_page(uint64 base, char *buf, int size) {
  // Header similar to user/memdump: "Memory at 0x...:"
  printf("Memory at ");
  printf("%p", (void*)base);
  printf(":\n");

  for(int i = 0; i < size; i++) {
    if(i % 16 == 0) {
      if(i > 0) printf("\n");
      // 4-hex-digit offset like memdump
      int off = i & 0xFFFF;
      printf("%04X: ", off);
    }
    printf("%02X ", (unsigned char)buf[i]);
  }
  printf("\n\n");
}

int main(int argc, char **argv) {
  const uint64 PGSIZE = 4096;
  const uint64 TOTAL_PAGES = (128 * 1024 * 1024) / PGSIZE; // 128MB / 4KB = 32768
  char buf[PGSIZE];
  struct pageinfo_user pi;
  int printed = 0;
  int dump_content = 0;

  if(argc > 1 && strcmp(argv[1], "-c") == 0) {
    dump_content = 1;
  }

  printf("Scanning physical memory...\n");
  uint64 i;
  for(i = 0; i < TOTAL_PAGES; i++) {
    uint64 pa = i * PGSIZE;
    int r = pageinfo_phys((uint64)&pi, pa);
    if(r < 0) continue; // invalid physical address or not available
    if(pi.type == 0) continue; // skip free pages

  // Print page info
  printf("\n=== Page %lu (0x%lx) ===\n", (unsigned long)i, (unsigned long)pa);
    printf("Type: %s(%d)\n", type_name(pi.type), pi.type);
    printf("Owner PID: %d\n", pi.owner_pid);
  printf("Mapped VA: 0x%lx\n", (unsigned long)pi.mapped_va);
  printf("Tag: %s\n", pi.tag);
  printf("Reference count: %lu\n", (unsigned long)pi.ref);
  printf("Allocation tick: %lu\n", (unsigned long)pi.alloc_tick);

    // If -c flag is provided, also dump the page contents
      if(dump_content) {
        // To read physical memory contents from userland, ask the kernel to
        // kread from the kernel virtual address that maps this physical page.
        // In xv6, kernel virtual base KERNBASE = 0x80000000 and physical
        // address pa is mapped at KERNBASE + pa.
        const uint64 KERNBASE = 0x80000000ULL;
        uint64 kva = KERNBASE + pa;
        // kread(addr, len, dst) is available in userland.
        r = kread(kva, PGSIZE, buf);
        if(r >= 0) {
          dump_page(kva, buf, PGSIZE);
        } else {
          printf("failed to kread physical page 0x%lx (kva=0x%lx)\n", (unsigned long)pa, (unsigned long)kva);
        }
      }
    
    printed++;
  }

  if(!printed) {
    printf("No allocated/mapped pages found.\n");
  } else {
    printf("\nTotal pages found: %d\n", printed);
  }
  
  return 0;
}
