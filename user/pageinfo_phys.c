#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Get information about the page at physical address pa
int
pageinfo_phys(uint64 pa)
{
  return pageinfo(pa, 1);
}
