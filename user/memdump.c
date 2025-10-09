// Simple memory region dumper for xv6 userland
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

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

// Print a memory region. 'base' is the address to display as the
// start of the region (for kernel reads this will be the kernel
// virtual address requested), 'start' points to the buffer holding
// the bytes to print (may be in user space), and 'length' is how
// many bytes to print.
void
dump_memory_range(unsigned long base, char *start, int length)
{
  // print header: Memory at 0x...
  printf("Memory at ");
  printf("%p", (void*)base);
  printf(":\n");

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

int
main(int argc, char **argv)
{
  if(argc < 2) {
    printf("usage: memdump <addr> [length]\n");
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
    dump_memory_range(addr, buf, length);
    free(buf);
    exit(0);
  }
  dump_memory_range((unsigned long)addr, (char *)addr, length);
  exit(0);
}
