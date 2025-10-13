#
// memdump_phys / pageinfo VA/phys helpers
//
// This user utility queries the kernel's `pageinfo` subsystem and prints a
// single pageinfo entry for either a virtual address (`pageinfo_va`) or a
// physical address (`pageinfo_phys`). It is a small, safe wrapper that:
//  - calls `pageinfo_va(dst, vaddr)` to ask the kernel to resolve the
//    virtual address into a backing physical page and copy that page's
//    pageinfo into the provided user buffer, or
//  - calls `pageinfo_phys(dst, pa)` to fetch the pageinfo for a raw
//    physical address (the kernel verifies the PA is in-range and
//    page-aligned).
//
// Safety notes:
//  - pageinfo_phys checks that the PA is within the kernel's tracked
//    PHYSTOP and it will fail otherwise.
//  - This utility uses the kernel's bookkeeping; it does not attempt to
//    dereference physical addresses directly.
//
// The remainder of this file is an uncomplicated user-side wrapper and
// printer for the returned `pageinfo` struct.

// NOTE / INSIGHTS:
// - `memdump_phys` (and the underlying `pageinfo` syscall) return
//   metadata about the physical page: `type` (FREE/KERNEL/USER/etc),
//   `owner_pid` (best-effort PID associated with the allocation),
//   `mapped_va` (last-observed mapped virtual address, may be 0 if not
//   recorded), `tag` (allocation tag such as "kalloc"), `ref`, and
//   `alloc_tick`.
// - `mapped_va` can be 0 even when you queried by VA. The pageinfo
//   subsystem records mapping events when they occur; `mapped_va` is
//   a best-effort field and may be empty if the page was allocated but
//   not observed being mapped, or if bookkeeping wasn't triggered.
// - `owner_pid` is best-effort and reflects the PID supplied when the
//   page was allocated or registered; it is useful for debugging but
//   not a strict authority on exclusive ownership.
// - To verify the relationship between metadata and contents:
//     1) use `memdump_phys <pa> -p` to get the pageinfo for PA
//     2) use `memdump <kva>` on the kernel virtual address that maps
//        that PA (KERNBASE + pa) or `memdump <va>` for the process VA
//        to inspect bytes
// - Use `dumppi` to list all non-FREE tracked pages from the kernel
//   pageinfo table; if `dumppi` prints nothing, the kernel pageinfo
//   subsystem may be empty and you should fallback to `procstat`+
//   `get_pagemap` to discover process mappings.

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

// Parse a hex string into a uint64 value
uint64 parse_hex(char *s) {
  uint64 val = 0;
  // simple hex parser: allow 0x... or plain hex
  if(s[0]=='0' && s[1]=='x') s += 2;
  for(; *s; s++){
    char c = *s;
    val <<= 4;
    if(c >= '0' && c <= '9') val |= (c - '0');
    else if(c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
    else if(c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
    else break;
  }
  return val;
}

void print_pageinfo(struct pageinfo_user *pi, uint64 addr, int is_physical) {
  printf("pageinfo for %s=0x%lx:\n", is_physical ? "pa" : "va", addr);
  printf("  type=%s (%d) owner_pid=%d mapped_va=0x%lx tag=%s ref=%lu alloc_tick=%lu\n",
    type_name(pi->type), pi->type, pi->owner_pid, pi->mapped_va, pi->tag, pi->ref, pi->alloc_tick);
}

int main(int argc, char **argv)
{
  if(argc < 2 || argc > 3){
    printf("usage: memdump_phys <virtual-address-hex> [-p]\n");
    printf("       memdump_phys <physical-address-hex> -p\n");
    printf("Options:\n");
    printf("  -p    Treat the address as a physical address\n");
    return 1;
  }

  int is_physical = 0;
  if(argc == 3 && strcmp(argv[2], "-p") == 0) {
    is_physical = 1;
  }

  // Parse the address from argv[1]
  uint64 addr = parse_hex(argv[1]);

  struct pageinfo_user pi;
  int r;
  
  if(is_physical) {
    r = pageinfo_phys((uint64)&pi, addr);
    if(r < 0){
      printf("pageinfo_phys failed for physical address 0x%lx\n", addr);
      return 1;
    }
  } else {
    r = pageinfo_va((uint64)&pi, addr);
    if(r < 0){
      printf("pageinfo_va failed for virtual address 0x%lx\n", addr);
      return 1;
    }
  }

  print_pageinfo(&pi, addr, is_physical);
  return 0;
}
