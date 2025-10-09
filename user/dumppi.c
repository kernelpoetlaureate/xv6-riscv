/*
 * dumppi.c
 * Print pageinfo for all allocated/mapped physical pages.
 * This mirrors the behavior of dump_all_pageinfo.c but is provided as the
 * dumppi user utility.
 */

#include "types.h"
#include "user.h"

#define PAGEINFO_TAGLEN 16
struct pageinfo_user {
  unsigned char type;
  int owner_pid;
  uint64 mapped_va;
  char tag[PAGEINFO_TAGLEN];
  uint64 alloc_tick;
  uint64 ref;
};

static const char *type_name(unsigned char t){
  switch(t){
    case 0: return "FREE";
    case 1: return "KERNEL";
    case 2: return "USER";
    case 3: return "BCACHE";
    case 4: return "PAGETBL";
    case 0xff: return "UNKNOWN";
    default: return "?";
  }
}

int
main(int argc, char **argv)
{
  const uint64 PGSIZE = 4096ULL;
  const uint64 TOTAL_PAGES = (128ULL * 1024 * 1024) / PGSIZE; // 128MB / 4KB

  struct pageinfo_user pi;
  int printed = 0;

  for(uint64 i = 0; i < TOTAL_PAGES; i++){
    uint64 pa = i * PGSIZE;
    int r = pageinfo_phys((uint64)&pi, pa);
    if(r < 0) continue; // invalid physical address or not available
    if(pi.type == 0) continue; // free
    printf("pa=0x%lx type=%s(%d) owner=%d mapped_va=0x%lx tag=%s ref=%lu alloc_tick=%lu\n",
      pa, type_name(pi.type), pi.type, pi.owner_pid, pi.mapped_va, pi.tag, pi.ref, pi.alloc_tick);
    printed++;
  }
  if(!printed) printf("No allocated/mapped pages found.\n");
  return 0;
}
