#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // Placeholder: kernel must be compiled with print_kmem_stats() available
  // For now this simply prints a message; later you can add a syscall to
  // invoke print_kmem_stats() from userland.
  printf("Physical Memory Information:\n");
  printf("(kernel must have logging enabled; try reading kernel console output)\n");
  exit(0);
}
