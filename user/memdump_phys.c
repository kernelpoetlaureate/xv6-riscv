#include "types.h"
#include "user.h"
#include "kernel/fcntl.h"

// user-side mirror of kernel struct pageinfo
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

int main(int argc, char **argv)
{
  if(argc < 2){
    printf("usage: memdump_phys <virtual-address-hex>\n");
    return 1;
  }

  // parse virtual address from argv[1]
  uint64 vaddr = 0;
  // simple hex parser: allow 0x... or plain hex
  char *s = argv[1];
  if(s[0]=='0' && s[1]=='x') s += 2;
  for(; *s; s++){
    char c = *s;
    vaddr <<= 4;
    if(c >= '0' && c <= '9') vaddr |= (c - '0');
    else if(c >= 'a' && c <= 'f') vaddr |= (c - 'a' + 10);
    else if(c >= 'A' && c <= 'F') vaddr |= (c - 'A' + 10);
    else break;
  }

  struct pageinfo_user pi;
  int r = pageinfo_va((uint64)&pi, vaddr);
  if(r < 0){
    printf("pageinfo_va failed for vaddr 0x%lx\n", vaddr);
    return 1;
  }

  printf("pageinfo for va=0x%lx:\n", vaddr);
  printf("  type=%s (%d) owner_pid=%d mapped_va=0x%lx tag=%s ref=%lu alloc_tick=%lu\n",
    type_name(pi.type), pi.type, pi.owner_pid, pi.mapped_va, pi.tag, pi.ref, pi.alloc_tick);
  return 0;
}
