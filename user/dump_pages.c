#include "types.h"
#include "user.h"

int
main(int argc, char **argv)
{
  // Trigger kernel dump of pageinfo (mode 2 = dump all)
  if(pageinfo(0, 2) < 0){
    printf("dump_pages: pageinfo syscall failed\n");
    return 1;
  }
  return 0;
}
