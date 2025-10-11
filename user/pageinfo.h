// User-visible page info structure; mirrors kernel/pageinfo.h
#ifndef _USER_PAGEINFO_H_
#define _USER_PAGEINFO_H_

#include "types.h"

struct page_info {
  uint64 va;    // virtual address (page-aligned)
  uint64 pa;    // physical address
  uint16 flags; // low bits of PTE
  uint16 refcount; // optional: 0 if unknown
};

#endif // _USER_PAGEINFO_H_
