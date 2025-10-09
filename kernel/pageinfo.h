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

struct pageinfo {
  unsigned char type;    // one of PGTYPE_*
  int owner_pid;         // pid if owned by a process, 0 for kernel, -1 unknown
  uint64 mapped_va;      // last virtual address mapped (if applicable)
  char tag[PAGEINFO_TAGLEN]; // short tag like "kalloc" or "uvm"
  uint64 alloc_tick;     // tick counter when allocated (optional)
  uint64 ref;            // optional reference counter
};

// initialize pageinfo subsystem
void pageinfo_init(void);

// mark a physical page as allocated by caller; pa must be PGSIZE-aligned
void pageinfo_set_alloc(void *pa, unsigned char type, int owner_pid, const char *tag);

// mark a physical page as freed
void pageinfo_set_free(void *pa);

// record that a physical page was mapped to a given virtual address for a pid
void pageinfo_set_mapped(void *pa, uint64 va, int owner_pid, unsigned char type);

// print pageinfo summary to console (for debugging)
void pageinfo_print_all(void);

// copy up to max entries of pageinfo into user-space buffer at dst.
// returns number of entries copied or -1 on error.
int pageinfo_copy_to_user(uint64 dst, int max);

// copy a single pageinfo entry for physical page at pa into user buffer dst.
// returns 0 on success, -1 on error.
int pageinfo_copy_entry_to_user(uint64 dst, void *pa);

#endif
