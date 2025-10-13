// memmap: print an ASCII birds-eye map of physical pages with robust fallback
//
// Usage:
//   memmap [-w width] [-b pages_per_block]
// Defaults: width=64 pages per row, pages_per_block=1 (one char per page)
//
// This program tries multiple approaches to build a memory map:
// 1. pageinfo_phys syscall scanning (kernel pageinfo tracking)
// 2. procstat + get_pagemap fallback (per-process page tables)
// 3. Provide helpful tips if all methods fail

#include "types.h"
#include "user.h"
#include "procstat.h"
#include "pageinfo.h"

// Match kernel definitions  
#define PGTYPE_FREE 0
#define PGTYPE_KERNEL 1
#define PGTYPE_USER 2
#define PGTYPE_BCACHE 3
#define PGTYPE_PAGETABLE 4

#define PGSIZE 4096
#define MAX_PROCS 64

// Userspace pageinfo layout (matches kernel pageinfo.h)
struct pageinfo_user {
  unsigned char type;
  int owner_pid;
  uint64 pa;            // physical address (added)
  uint64 mapped_va;
  char tag[16];
  uint64 alloc_tick;
  uint64 ref;
};

// Simple xrealloc helper (since we don't have standard library)
void* xrealloc(void* ptr, uint64 old_size, uint64 new_size) {
  if (new_size == 0) {
    if (ptr) free(ptr);
    return 0;
  }
  void* new_ptr = malloc(new_size);
  if (!new_ptr) return 0;
  if (ptr && old_size > 0) {
    uint64 copy_size = old_size < new_size ? old_size : new_size;
    char* src = (char*)ptr;
    char* dst = (char*)new_ptr;
    for (uint64 i = 0; i < copy_size; i++) {
      dst[i] = src[i];
    }
    free(ptr);
  }
  return new_ptr;
}

// Global owner map
struct {
  int pid;
  char symbol;
} *owner_map = 0;
int owner_map_size = 0;
// owner_map is filled deterministically later

// Try pageinfo_phys approach
int try_pageinfo_phys(struct pageinfo_user **pages_out, int *count_out) {
  struct pageinfo_user *pages = malloc(1024 * sizeof(*pages));
  if (!pages) return -1;
  
  int pages_count = 0;
  int pages_capacity = 1024;
  
  // Skip initial failing PAs and scan forward
  int found_any = 0;
  for (uint64 pa = 0; pa < 0x10000000; pa += PGSIZE) {
    struct pageinfo_user pi;
    int ret = pageinfo_phys((uint64)&pi, pa);
    if (ret >= 0) {
      found_any = 1;
      if (pages_count >= pages_capacity) {
        pages_capacity *= 2;
        pages = (void*)xrealloc(pages, 
          pages_count * sizeof(*pages),
          pages_capacity * sizeof(*pages));
        if (!pages) return -1;
      }
      pi.pa = pa;
      pages[pages_count++] = pi;
    } else if (found_any) {
      // Hit the end of valid pages
      break;
    }
  }
  
  if (pages_count == 0) {
    free(pages);
    return -1;
  }
  
  *pages_out = pages;
  *count_out = pages_count;
  return 0;
}

// Fallback: use procstat + get_pagemap per process
int try_procstat_pagemap(struct pageinfo_user **pages_out, int *count_out) {
  struct procstat *procs = malloc(MAX_PROCS * sizeof(*procs));
  if (!procs) return -1;
  
  int nprocs = procstat((uint64)procs, MAX_PROCS);
  if (nprocs <= 0) {
    free(procs);
    return -1;
  }
  
  // Collect pages from all processes
  struct pageinfo_user *all_pages = malloc(4096 * sizeof(*all_pages));
  if (!all_pages) {
    free(procs);
    return -1;
  }
  int all_pages_count = 0;
  int all_pages_capacity = 4096;
  
  // Also track which physical pages we've seen
  struct {
    uint64 pa;
    int owner_pid;
    unsigned char type;
  } *pa_map = malloc(4096 * sizeof(*pa_map));
  if (!pa_map) {
    free(procs);
    free(all_pages);
    return -1;
  }
  int pa_map_count = 0;
  int pa_map_capacity = 4096;
  
  for (int i = 0; i < nprocs; i++) {
    if (procs[i].pid <= 0) continue;
    
    struct page_info *proc_pages = malloc(1024 * sizeof(*proc_pages));
    if (!proc_pages) continue;
    
    int got = get_pagemap(procs[i].pid, (uint64)proc_pages, 1024);
    if (got > 0) {
      for (int j = 0; j < got; j++) {
        // Check if we already have this PA
        int found = 0;
        for (int k = 0; k < pa_map_count; k++) {
          if (pa_map[k].pa == proc_pages[j].pa) {
            found = 1;
            break;
          }
        }
        
        if (!found) {
          // Add to PA map
          if (pa_map_count >= pa_map_capacity) {
            pa_map_capacity *= 2;
            pa_map = (void*)xrealloc(pa_map,
              pa_map_count * sizeof(*pa_map),
              pa_map_capacity * sizeof(*pa_map));
            if (!pa_map) break;
          }
          
          pa_map[pa_map_count].pa = proc_pages[j].pa;
          pa_map[pa_map_count].owner_pid = procs[i].pid;
          pa_map[pa_map_count].type = PGTYPE_USER;
          pa_map_count++;
          
          // Convert to pageinfo_user format
          if (all_pages_count >= all_pages_capacity) {
            all_pages_capacity *= 2;
            all_pages = (void*)xrealloc(all_pages,
              all_pages_count * sizeof(*all_pages),
              all_pages_capacity * sizeof(*all_pages));
            if (!all_pages) break;
          }
          
          struct pageinfo_user *pi = &all_pages[all_pages_count++];
          pi->type = PGTYPE_USER;
          pi->owner_pid = procs[i].pid;
          pi->pa = proc_pages[j].pa;
          pi->mapped_va = proc_pages[j].va;
          pi->alloc_tick = 0;
          pi->ref = proc_pages[j].refcount;
          // Clear tag
          for (int t = 0; t < 16; t++) pi->tag[t] = 0;
        }
      }
    }
    
    free(proc_pages);
  }
  
  free(procs);
  free(pa_map);
  
  if (all_pages_count == 0) {
    free(all_pages);
    return -1;
  }
  
  *pages_out = all_pages;
  *count_out = all_pages_count;
  return 0;
}

int main(int argc, char *argv[]) {
  int width = 64;
  int pages_per_block = 1;
  
  // Parse command line
  for (int i = 1; i < argc; i++) {
    if (i + 1 < argc && strcmp(argv[i], "-w") == 0) {
      width = atoi(argv[i + 1]);
      i++;
    } else if (i + 1 < argc && strcmp(argv[i], "-b") == 0) {
      pages_per_block = atoi(argv[i + 1]);
      i++;
    } else {
      printf("usage: %s [-w width] [-b pages_per_block]\n", argv[0]);
      return 1;
    }
  }
  
  if (width <= 0) width = 64;
  if (pages_per_block <= 0) pages_per_block = 1;
  
  struct pageinfo_user *pages = 0;
  int pages_count = 0;
  
  // Try pageinfo_phys first
  printf("Trying pageinfo_phys scan...\n");
  if (try_pageinfo_phys(&pages, &pages_count) == 0) {
    printf("Success: found %d pages via pageinfo_phys\n", pages_count);
  } else {
    printf("pageinfo_phys failed, trying procstat+get_pagemap fallback...\n");
    if (try_procstat_pagemap(&pages, &pages_count) == 0) {
      printf("Success: found %d pages via procstat+get_pagemap\n", pages_count);
      // For pages discovered via get_pagemap, refine type/tag by calling pageinfo_phys
      for (int i = 0; i < pages_count; i++) {
        struct pageinfo_user tmp;
        int r = pageinfo_phys((uint64)&tmp, pages[i].pa);
        if (r >= 0) {
          // adopt kernel's authoritative type/owner when available
          pages[i].type = tmp.type;
          pages[i].owner_pid = tmp.owner_pid;
          for (int t = 0; t < 16; t++) pages[i].tag[t] = tmp.tag[t];
        }
      }
    } else {
      printf("memmap: all approaches failed - no memory map available\n");
      printf("Tip: try running 'alloctest' or other programs to allocate memory first\n");
      return 1;
    }
  }

  // Sort pages by physical address for deterministic output
  for (int i = 0; i < pages_count; i++) {
    for (int j = i+1; j < pages_count; j++) {
      if (pages[j].pa < pages[i].pa) {
        struct pageinfo_user t = pages[i]; pages[i] = pages[j]; pages[j] = t;
      }
    }
  }
  
  printf("\nPhysical Memory Map (%d pages, %d pages per block, %d blocks per row)\n", 
         pages_count, pages_per_block, width);
  printf("Legend: . = FREE, # = KERNEL, B = BCACHE, T = PAGETBL, 0-9A-Za-z = USER (pid)\n\n");
  
  // Deterministic owner symbol assignment: collect unique PIDs and sort
  int max_owners = 128;
  int *owners = malloc(max_owners * sizeof(int));
  int owners_count = 0;
  for (int i = 0; i < pages_count; i++) {
    int pid = pages[i].owner_pid;
    if (pid == 0) continue; // kernel
    int found = 0;
    for (int j = 0; j < owners_count; j++) if (owners[j] == pid) { found = 1; break; }
    if (!found) {
      if (owners_count >= max_owners) {
        owners = (int*)xrealloc(owners, max_owners * sizeof(int), (max_owners*2) * sizeof(int));
        if (!owners) break;
        max_owners *= 2;
      }
      owners[owners_count++] = pid;
    }
  }
  // simple sort owners
  for (int i = 0; i < owners_count; i++) for (int j = i+1; j < owners_count; j++) if (owners[j] < owners[i]) { int t = owners[i]; owners[i] = owners[j]; owners[j] = t; }
  // assign symbols deterministically
  for (int i = 0; i < owners_count; i++) {
    // ensure owner_map contains this pid at same index
    owner_map = (void*)xrealloc(owner_map, owner_map_size * sizeof(*owner_map), (owner_map_size+1) * sizeof(*owner_map));
    if (!owner_map) break;
    owner_map[owner_map_size].pid = owners[i];
    char symbols[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    owner_map[owner_map_size].symbol = symbols[owner_map_size % 62];
    owner_map_size++;
  }

  // Count by type
  int counts[256] = {0};
  
  // Generate map
  int blocks = (pages_count + pages_per_block - 1) / pages_per_block;
  for (int b = 0; b < blocks; b++) {
    if (b > 0 && b % width == 0) {
      printf("\n");
    }
    // print starting PA for this row (every width or at beginning)
    if (b % width == 0) {
      uint64 start_idx = b * pages_per_block;
      if (start_idx < (uint64)pages_count) {
        // use %p which xv6 printf supports for full-width hex pointers
        printf("%p: ", (void*)pages[start_idx].pa);
      }
    }
    
    // Determine block character - find first non-free type
    char ch = '.';
    for (int p = 0; p < pages_per_block && b * pages_per_block + p < pages_count; p++) {
      struct pageinfo_user *pi = &pages[b * pages_per_block + p];
      counts[pi->type]++;
      
      if (ch == '.') { // Only override if still FREE
        switch (pi->type) {
          case PGTYPE_FREE:
            ch = '.';
            break;
          case PGTYPE_KERNEL:
            ch = '#';
            break;
          case PGTYPE_BCACHE:
            ch = 'B';
            break;
          case PGTYPE_PAGETABLE:
            ch = 'T';
            break;
          case PGTYPE_USER: {
            char s = '?';
            // find symbol in owner_map for pid
            for (int oi = 0; oi < owner_map_size; oi++) if (owner_map[oi].pid == pi->owner_pid) { s = owner_map[oi].symbol; break; }
            ch = s;
            break;
          }
          default:
            ch = '?';
            break;
        }
      }
    }
    printf("%c", ch);
  }
  printf("\n\nCounts:\n");
  printf("FREE: %d, KERNEL: %d, BCACHE: %d, PAGETABLE: %d, USER: %d\n",
         counts[PGTYPE_FREE], counts[PGTYPE_KERNEL], 
         counts[PGTYPE_BCACHE], counts[PGTYPE_PAGETABLE], counts[PGTYPE_USER]);
  
  if (owner_map_size > 0) {
    printf("\nOwner symbols:\n");
    for (int i = 0; i < owner_map_size; i++) {
      printf("  %c = PID %d\n", owner_map[i].symbol, owner_map[i].pid);
    }

    // Verification: count pages per owner and print counts so numbers are not placeholders
    int *owner_counts = malloc(owner_map_size * sizeof(int));
    if (owner_counts) {
      for (int i = 0; i < owner_map_size; i++) owner_counts[i] = 0;
      for (int i = 0; i < pages_count; i++) {
        if (pages[i].type != PGTYPE_USER) continue;
        for (int j = 0; j < owner_map_size; j++) {
          if (owner_map[j].pid == pages[i].owner_pid) { owner_counts[j]++; break; }
        }
      }
      printf("\nPer-owner page counts:\n");
      for (int i = 0; i < owner_map_size; i++) {
        printf("  %c (PID %d) -> %d pages\n", owner_map[i].symbol, owner_map[i].pid, owner_counts[i]);
      }
      free(owner_counts);
    }
  }
  
  free(pages);
  free(owner_map);
  return 0;
}