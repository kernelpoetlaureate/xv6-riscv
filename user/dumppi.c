/*
 * dumppi.c
 * Print pageinfo for all allocated/mapped physical pages.
 * This mirrors the behavior of dump_all_pageinfo.c but is provided as the
 * dumppi user utility.
 */

#include "types.h"
#include "user.h"

#define PAGEINFO_TAGLEN 16
struct pageinfo_user {
  unsigned char type;
  int owner_pid;
  uint64 mapped_va;
  char tag[PAGEINFO_TAGLEN];
  uint64 alloc_tick;
  uint64 ref;
};

static const char *type_name(unsigned char t){
  switch(t){
    case 0: return "FREE";
    case 1: return "KERNEL";
    case 2: return "USER";
    case 3: return "BCACHE";
    case 4: return "PAGETBL";
    case 0xff: return "UNKNOWN";
    default: return "?";
  }
}

int
main(int argc, char **argv)
{
  const uint64 PGSIZE = 4096ULL;
  const uint64 TOTAL_PAGES = (128ULL * 1024 * 1024) / PGSIZE; // 128MB / 4KB

  // New: support grouping/granularity like memmap
  int width = 64;
  int pages_per_char = 1; // -g
  int group_by_owner = 0;  // -O
  int any_group = 0; // whether the user requested grouped output

  for (int i = 1; i < argc; i++) {
    if (i + 1 < argc && strcmp(argv[i], "-w") == 0) {
      width = atoi(argv[i+1]);
      i++;
    } else if (i + 1 < argc && strcmp(argv[i], "-g") == 0) {
      pages_per_char = atoi(argv[i+1]);
      if (pages_per_char > 0) any_group = 1;
      i++;
    } else if (strcmp(argv[i], "-O") == 0 || strcmp(argv[i], "--owners") == 0) {
      group_by_owner = 1;
      any_group = 1;
    } else {
      printf("usage: dumppi [-w width] [-g pages_per_char] [-O]\n");
      return 1;
    }
  }

  // If no grouping requested, preserve original dumppi behavior: list non-FREE pages
  if (!any_group) {
    struct pageinfo_user pi;
    int printed = 0;
    for (uint64 i = 0; i < TOTAL_PAGES; i++) {
      uint64 pa = i * PGSIZE;
      int r = pageinfo_phys((uint64)&pi, pa);
      if (r < 0) continue;
      if (pi.type == 0) continue;
      printf("pa=0x%lx type=%s(%d) owner=%d mapped_va=0x%lx tag=%s ref=%lu alloc_tick=%lu\n",
        pa, type_name(pi.type), pi.type, pi.owner_pid, pi.mapped_va, pi.tag, pi.ref, pi.alloc_tick);
      printed++;
    }
    if (!printed) printf("No allocated/mapped pages found.\n");
    return 0;
  }

  // --- grouped output path ---
  if (width <= 0) width = 64;
  if (pages_per_char <= 0) pages_per_char = 1;

  // small xrealloc helper
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
      for (uint64 i = 0; i < copy_size; i++) dst[i] = src[i];
      free(ptr);
    }
    return new_ptr;
  }

  // Collect pageinfo entries with coarse-first scan to avoid spamming kernel
  struct pageinfo_user *pages = malloc(1024 * sizeof(*pages));
  if (!pages) return 1;
  int pages_count = 0;
  int pages_capacity = 1024;

  const uint64 SEARCH_LIMIT = 0x10000000;
  const uint64 COARSE_STEP = 0x100000; // 1MB
  int found_any = 0;
  uint64 probe_pa;
  struct pageinfo_user tmp;

  for (probe_pa = 0; probe_pa < SEARCH_LIMIT; probe_pa += COARSE_STEP) {
    int r = pageinfo_phys((uint64)&tmp, probe_pa);
    if (r >= 0) { found_any = 1; break; }
  }
  if (!found_any) {
    printf("No allocated/mapped pages found.\n");
    free(pages);
    return 0;
  }

  uint64 start_pa = (probe_pa >= COARSE_STEP) ? probe_pa - COARSE_STEP : 0;
  for (uint64 pa = start_pa; pa < SEARCH_LIMIT; pa += PGSIZE) {
    int r = pageinfo_phys((uint64)&tmp, pa);
    if (r >= 0) {
      if (pages_count >= pages_capacity) {
        pages_capacity *= 2;
        pages = (void*)xrealloc(pages, pages_count * sizeof(*pages), pages_capacity * sizeof(*pages));
        if (!pages) return 1;
      }
      tmp.mapped_va = tmp.mapped_va; // no-op, keep fields
      pages[pages_count++] = tmp;
    } else if (pages_count > 0) {
      // reached end of tracked region
      break;
    }
  }

  if (pages_count == 0) {
    printf("No allocated/mapped pages found.\n");
    free(pages);
    return 0;
  }

  // We need PAs stored; pageinfo_phys doesn't return PA so we infer from index
  // Re-run assignment of PA by walking start_pa + idx*PGSIZE for each collected entry
  for (int i = 0; i < pages_count; i++) {
    // first collected page corresponds to first success; compute its PA
    // We cannot guarantee continuous collection if kernel allows sparse PAs; instead
    // compute PA as start_pa + k*PGSIZE while matching sequence of successes.
    // Simplest approach: perform a quick scan to recompute PA for each saved entry
    // by calling pageinfo_phys again and matching addresses — but that's expensive.
    // Instead, we store PAs when collecting: modify above to store PA in tag? Not possible.
    // Safer: re-run the fine scan and rebuild pages with PA recorded.
  }

  // Rebuild pages array capturing PA for each pageinfo success
  struct {
    uint64 pa;
    struct pageinfo_user pi;
  } *pages_with_pa = malloc(pages_count * sizeof(*pages_with_pa));
  if (!pages_with_pa) { free(pages); return 1; }

  int idx = 0;
  for (uint64 pa = start_pa; pa < SEARCH_LIMIT && idx < pages_count; pa += PGSIZE) {
    int r = pageinfo_phys((uint64)&tmp, pa);
    if (r >= 0) {
      pages_with_pa[idx].pa = pa;
      pages_with_pa[idx].pi = tmp;
      idx++;
    }
  }
  pages_count = idx;

  // Sort by PA (simple insertion/sort since counts are moderate)
  for (int i = 0; i < pages_count; i++) for (int j = i+1; j < pages_count; j++) {
    if (pages_with_pa[j].pa < pages_with_pa[i].pa) {
      typeof(pages_with_pa[0]) t = pages_with_pa[i]; pages_with_pa[i] = pages_with_pa[j]; pages_with_pa[j] = t;
    }
  }

  // Deterministic owner symbol assignment
  int max_owners = 128;
  int *owners = malloc(max_owners * sizeof(int));
  int owners_count = 0;
  for (int i = 0; i < pages_count; i++) {
    int pid = pages_with_pa[i].pi.owner_pid;
    if (pid == 0) continue;
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
  for (int i = 0; i < owners_count; i++) for (int j = i+1; j < owners_count; j++) if (owners[j] < owners[i]) { int t = owners[i]; owners[i] = owners[j]; owners[j] = t; }
  struct { int pid; char symbol; } *owner_map = 0;
  int owner_map_size = 0;
  for (int i = 0; i < owners_count; i++) {
    owner_map = (void*)xrealloc(owner_map, owner_map_size * sizeof(*owner_map), (owner_map_size+1) * sizeof(*owner_map));
    if (!owner_map) break;
    owner_map[owner_map_size].pid = owners[i];
    char symbols[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    owner_map[owner_map_size].symbol = symbols[owner_map_size % 62];
    owner_map_size++;
  }

  // Print header
  if (group_by_owner) {
    printf("\nPhysical Memory Map (grouped by owner runs, %d pages total)\n", pages_count);
    printf("Legend: . = FREE, # = KERNEL, B = BCACHE, T = PAGETBL, 0-9A-Za-z = USER (pid)\n\n");
  } else {
    printf("\nPhysical Memory Map (%d pages, %d pages per char, %d chars per row)\n", pages_count, pages_per_char, width);
    printf("Legend: . = FREE, # = KERNEL, B = BCACHE, T = PAGETBL, 0-9A-Za-z = USER (pid)\n\n");
  }

  int counts[256]; for (int i = 0; i < 256; i++) counts[i] = 0;

  if (group_by_owner) {
    int chars = 0;
    for (int i = 0; i < pages_count; ) {
      struct pageinfo_user *pi = &pages_with_pa[i].pi;
      uint64 pa = pages_with_pa[i].pa;
      int j = i+1;
      while (j < pages_count) {
        if (pages_with_pa[j].pi.type != pi->type) break;
        if (pages_with_pa[j].pa != pages_with_pa[j-1].pa + PGSIZE) break;
        if (pi->type == 2 && pages_with_pa[j].pi.owner_pid != pi->owner_pid) break;
        j++;
      }
      if (chars > 0 && chars % width == 0) printf("\n");
      if (chars % width == 0) printf("%p: ", (void*)pa);
      char ch = '.';
      switch (pi->type) {
        case 0: ch = '.'; break;
        case 1: ch = '#'; break;
        case 3: ch = 'B'; break;
        case 4: ch = 'T'; break;
        case 2: {
          char s = '?';
          for (int oi = 0; oi < owner_map_size; oi++) if (owner_map[oi].pid == pi->owner_pid) { s = owner_map[oi].symbol; break; }
          ch = s; break;
        }
        default: ch = '?'; break;
      }
      for (int k = i; k < j; k++) counts[pages_with_pa[k].pi.type]++;
      printf("%c", ch);
      chars++;
      i = j;
    }
    printf("\n");
  } else {
    int blocks = (pages_count + pages_per_char - 1) / pages_per_char;
    for (int b = 0; b < blocks; b++) {
      if (b > 0 && b % width == 0) printf("\n");
      if (b % width == 0) {
        uint64 start_idx = b * pages_per_char;
        if (start_idx < (uint64)pages_count) printf("%p: ", (void*)pages_with_pa[start_idx].pa);
      }
      char ch = '.';
      for (int p = 0; p < pages_per_char && b * pages_per_char + p < pages_count; p++) {
        struct pageinfo_user *pi = &pages_with_pa[b * pages_per_char + p].pi;
        counts[pi->type]++;
        if (ch == '.') {
          switch (pi->type) {
            case 0: ch = '.'; break;
            case 1: ch = '#'; break;
            case 3: ch = 'B'; break;
            case 4: ch = 'T'; break;
            case 2: {
              char s = '?';
              for (int oi = 0; oi < owner_map_size; oi++) if (owner_map[oi].pid == pi->owner_pid) { s = owner_map[oi].symbol; break; }
              ch = s; break;
            }
            default: ch = '?'; break;
          }
        }
      }
      printf("%c", ch);
    }
    printf("\n");
  }

  printf("\nCounts:\n");
  printf("FREE: %d, KERNEL: %d, BCACHE: %d, PAGETABLE: %d, USER: %d\n",
    counts[0], counts[1], counts[3], counts[4], counts[2]);

  if (owner_map_size > 0) {
    printf("\nOwner symbols:\n");
    for (int i = 0; i < owner_map_size; i++) printf("  %c = PID %d\n", owner_map[i].symbol, owner_map[i].pid);
  }

  free(pages);
  free(pages_with_pa);
  free(owners);
  free(owner_map);
  return 0;
}
