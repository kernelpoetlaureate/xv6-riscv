// Simple memory region dumper for xv6 userland
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

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

void
dump_memory_range(char *start, int length)
{
  printf("Memory at %p:\n", start);
  for(int i = 0; i < length && i < 64; i++) { // Limit output
    if(i % 16 == 0) printf("\n%04x: ", i);
    unsigned char b = (unsigned char)start[i];
    printf("%02x ", b & 0xFF);
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

  dump_memory_range((char *)addr, length);
  exit(0);
}
