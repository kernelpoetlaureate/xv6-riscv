// Per-physical-page metadata for debugging/inspection
#ifndef PAGEINFO_H
#define PAGEINFO_H

#include "types.h"

/* forward declaration to avoid header redefinition issues */
struct spinlock;

#define PAGEINFO_TAGLEN 16

// page types
#define PGTYPE_FREE 0
#define PGTYPE_KERNEL 1
#define PGTYPE_USER 2
#define PGTYPE_BCACHE 3
#define PGTYPE_PAGETABLE 4
#define PGTYPE_UNKNOWN 0xff

// Page information tracking
#define NPAGE (PHYSTOP / PGSIZE)
#define PI_NPAGES NPAGE
#define PA2IDX(pa) (((uint64)(pa)) / PGSIZE)
#define IDX2PA(idx) ((uint64)(idx) * PGSIZE)

struct pageinfo {
  unsigned char type;             // PGTYPE_*
  int owner_pid;                  // pid that owns this page (0 for kernel)
  uint64 mapped_va;               // last-mapped virtual address (if any)
  char tag[PAGEINFO_TAGLEN];      // short tag describing allocation
  uint64 alloc_tick;              // tick when allocated (optional)
  uint64 ref;                     // reference count / usages
};

extern struct pageinfo pi_array[PI_NPAGES];

// initialize pageinfo subsystem
void pageinfo_init(void);

// register a page allocation/free from kalloc/kfree
void register_page_allocation(uint64 pa, int pid);
void register_page_free(uint64 pa);

// debug: dump all tracked pages to console
int dump_pageinfo(void);

// copy up to max entries of pageinfo into user-space buffer at dst.
// returns number of entries copied or -1 on error.
int pageinfo_copy_to_user(uint64 dst, int max);

// copy a single pageinfo entry for physical page at pa into user buffer dst.
// returns 0 on success, -1 on error.
int pageinfo_copy_entry_to_user(uint64 dst, void *pa);

// record that a physical page at pa was mapped at virtual address va
// owner is pid (0 for kernel), and t is PGTYPE_*
void pageinfo_set_mapped(void *pa, uint64 va, int owner, unsigned char t);

#endif
