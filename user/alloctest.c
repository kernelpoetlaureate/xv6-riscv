// A test program that allocates memory to verify pageinfo tracking

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define ALLOC_SIZE (10 * 4096)  // 10 pages

int
main(int argc, char *argv[])
{
  printf("Allocating %d bytes of memory...\n", ALLOC_SIZE);
  
  void *mem = malloc(ALLOC_SIZE);
  if (!mem) {
    printf("Failed to allocate memory!\n");
    exit(1);
  }
  
  // Touch each page to ensure it's allocated
  char *ptr = (char*)mem;
  for (int i = 0; i < ALLOC_SIZE; i += 4096) {
    ptr[i] = 1;
  }
  
  printf("Memory allocated at %p\n", mem);
  printf("Run 'dumppi' now to see allocated pages\n");
  printf("Press Enter to continue and free memory...\n");
  
  // Keep the memory allocated while user runs dumppi
  char c;
  read(0, &c, 1);  // Wait for user input
  
  free(mem);
  printf("Memory freed\n");
  
  return 0;
}
