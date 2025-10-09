#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// Get information about the page at virtual address va
int
pageinfo_va(void *va)
{
  return pageinfo((uint64)va, 0);
}
