// Page map info exported to userspace
#ifndef _PAGEINFO_H_
#define _PAGEINFO_H_

#include "types.h"

struct page_info {
  uint64 va;    // virtual address (page-aligned)
  uint64 pa;    // physical address
  uint16 flags; // low bits of PTE
  uint16 refcount; // optional: 0 if unknown
};

#endif // _PAGEINFO_H_
#
// Per-physical-page metadata for debugging/inspection.
//
// The pageinfo subsystem maintains a compact array indexed by physical-page
// number (PA / PGSIZE). It is intended for debugging, inspection and
// best-effort bookkeeping of which physical pages are allocated, which
// process "owns" them, and where they are mapped. It is NOT the authoritative
// allocator data structure (the allocator uses kmem.freelist in kalloc.c),
// but it mirrors allocation/mapping events via hooks:
//   - register_page_allocation(pa, pid) is called by kalloc() to record a
//     newly allocated page and associate it with a PID (pid==0 means kernel).
//   - register_page_free(pa) is called by kfree() when a page is returned to
//     the free-list.
//   - pageinfo_set_mapped(pa, va, owner, t) is called when a page is mapped
//     into a page table (mappages), to record the mapping VA and page type.
//
// Important notes / semantics:
//  - The `owner_pid` field is best-effort and reflects the PID passed when
//    the allocation hook was executed. Kernel-owned pages use pid==0.
//  - The `mapped_va` field stores the last virtual address the pageinfo
//    observed the page being mapped at. It may be 0 if not mapped or not
//    observed.
//  - The `ref` counter is a cheap best-effort counter that increments on
//    allocation and when a mapping event is recorded. It is not a strict
//    reference-count for shared pages.
//  - pageinfo is sized to PHYSTOP/PGSIZE. Pages outside that physical range
//    (for example some MMIO ranges) are not represented in this table.
//
// Use this data for debugging and inspection. For programmatic correctness
// or allocation decisions, consult the allocator (`kalloc.c`) and VM
// code (`vm.c`).
//
// Example usage: `dump_pageinfo()` prints non-free entries and can be used
// to see which pages are currently in-use and who owns them.
//
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
  unsigned char type;             // PGTYPE_*: semantic classification
                                  //   - PGTYPE_FREE: page is on the allocator free-list
                                  //   - PGTYPE_KERNEL: kernel-owned data/text/bss
                                  //   - PGTYPE_USER: user process data (heap/stack)
                                  //   - PGTYPE_BCACHE: buffer cache backing a disk block
                                  //   - PGTYPE_PAGETABLE: a page-table page
                                  //   - PGTYPE_UNKNOWN: unknown / not classified
  int owner_pid;                  // pid that owns this page (0 for kernel).
                                  // This is best-effort: it reflects the PID
                                  // supplied when the allocation or mapping hook
                                  // was recorded. It is NOT authoritative: the
                                  // allocator itself does not consult this field.
  uint64 mapped_va;               // last-mapped virtual address (if any).
                                  // This records the last VA at which the
                                  // pageinfo subsystem observed this physical
                                  // page being mapped. It may be 0 if the page
                                  // is not currently mapped or the mapping event
                                  // wasn't recorded.
  char tag[PAGEINFO_TAGLEN];      // short tag describing allocation source
                                  // (for example "kalloc", "kalloc:ker").
                                  // This is for human-facing debugging only.
  uint64 alloc_tick;              // tick when allocated (optional).
                                  // Recorded under tickslock when
                                  // register_page_allocation() runs. This is
                                  // a best-effort timestamp useful for tracing
                                  // allocation order; it may wrap.
  uint64 ref;                     // cheap, best-effort reference/mapping count.
                                  // The counter is incremented on allocation
                                  // and when a mapping event is recorded. It is
                                  // NOT a strict reference count for shared
                                  // pages and should be used only for
                                  // heuristic/debugging purposes.
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
