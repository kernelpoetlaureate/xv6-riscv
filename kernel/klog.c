// lightweight kernel logging helpers

#include "types.h"
#include "riscv.h"
#include "defs.h"

// kbanner: prints a bold cyan title and a dim subtitle on the next line.
void
kbanner(const char *title, const char *subtitle)
{
  if(title){
    printf("\n\x1b[1;36m%s\x1b[0m\n", title);
  }
  if(subtitle){
    printf("\x1b[90m%s\x1b[0m\n", subtitle);
  }
}
