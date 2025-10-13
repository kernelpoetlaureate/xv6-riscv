// memmap: print an ASCII birds-eye map of physical pages
//
// Usage:
//   memmap [-w width] [-b pages_per_block]
// Defaults: width=64 pages per row, pages_per_block=1 (one char per page)
//
// This program calls the kernel `pageinfo_phys` syscall for each
// page-aligned physical address starting at 0 and continues until
// the syscall returns an error (out-of-range). It builds a compact
// ASCII representation where each character represents one or more
// physical pages and a legend maps owner PIDs to symbols.

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

// Page types used in kernel pageinfo
enum { PT_FREE = 0, PT_KERNEL = 1, PT_USER = 2, PT_BCACHE = 3, PT_PAGETBL = 4, PT_UNKNOWN = 0xff };

// Symbols used to represent owners. Index 0 reserved for FREE, 1 for KERNEL.
static const char owner_symbols[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

// owner map at file scope so helper can access it
struct owner_map { int pid; char sym; };
static int omcap = 32, omn = 0;
static struct owner_map *om = 0;

// simple realloc replacement: allocate newsize bytes, copy old, free old
static void *xrealloc(void *oldp, int oldbytes, int newbytes){
  void *p = malloc(newbytes);
  if(!p) return 0;
  if(oldp && oldbytes > 0){
    int n = oldbytes < newbytes ? oldbytes : newbytes;
    char *dst = (char*)p;
    char *src = (char*)oldp;
    for(int i=0;i<n;i++) dst[i] = src[i];
  }
  if(oldp) free(oldp);
  return p;
}

// Helper to find or assign symbol for an owner pid (C version)
static char get_symbol(int pid, unsigned char type) {
  if(type == PT_FREE) return '.';
  if(type == PT_KERNEL) return '#';
  // search
  for(int i=0;i<omn;i++) if(om[i].pid == pid) return om[i].sym;
  // assign new
  if(omn >= omcap){
    int newcap = omcap * 2;
    struct owner_map *newom = xrealloc(om, omcap * sizeof(*om), newcap * sizeof(*om));
    if(!newom){ printf("memmap: owner map realloc failed\n"); exit(1); }
    om = newom;
    omcap = newcap;
  }
  int idx = omn % (int)(sizeof(owner_symbols)-1);
  char s = owner_symbols[idx];
  om[omn].pid = pid;
  om[omn].sym = s;
  omn++;
  return s;
}

int
main(int argc, char **argv)
{
  int width = 64; // pages per row
  int pages_per_block = 1;

  // simple arg parsing
  for(int i=1;i<argc;i++){
    if(strcmp(argv[i], "-w") == 0 && i+1 < argc){ width = atoi(argv[++i]); if(width<=0) width = 64; }
    else if(strcmp(argv[i], "-b") == 0 && i+1 < argc){ pages_per_block = atoi(argv[++i]); if(pages_per_block<=0) pages_per_block = 1; }
    else { printf("usage: memmap [-w width] [-b pages_per_block]\n"); exit(1); }
  }

  // We'll query pageinfo_phys starting at pa=0 and stop on failure.
  // Collect entries in a dynamically grown array (malloc/realloc).
  int cap = 1024;
  int n = 0;
  struct pageinfo_user *pages = malloc(sizeof(*pages) * cap);
  if(!pages){ printf("memmap: allocation failed\n"); exit(1); }

  uint64 pa = 0;
  // If pageinfo_phys fails at pa=0 (some kernels don't accept pa==0),
  // keep scanning forward until we find the first valid pageinfo entry
  // or give up after a reasonable number of attempts.
  int first_found = 0;
  int attempts = 0;
  int max_initial_attempts = 16384; // scan up to 16384 pages (~64MB) to find first valid
  while(1){
    struct pageinfo_user pi;
    int r = pageinfo_phys((uint64)&pi, pa);
    if(r < 0){
      if(!first_found){
        attempts++;
        if(attempts > max_initial_attempts) break; // nothing found in range
        pa += 4096;
        continue;
      }
      break; // we've seen pages and now out-of-range -> stop
    }
    first_found = 1;
    attempts = 0;
    if(n >= cap){ int newcap = cap * 2; struct pageinfo_user *newp = xrealloc(pages, cap * sizeof(*pages), newcap * sizeof(*pages)); if(!newp){ printf("memmap: realloc failed\n"); exit(1); } pages = newp; cap = newcap; }
    pages[n++] = pi;
    pa += 4096; // PGSIZE
  }

  if(n == 0){ printf("memmap: no pages found (pageinfo_phys failed at pa=0)\n"); free(pages); exit(1); }

  // Map owner_pid -> symbol index. We reserve symbol 0 for FREE ('.') and symbol 1 for KERNEL ('#').
  // We'll build a small table of seen owners.
  omcap = 32; omn = 0;
  om = malloc(sizeof(*om)*omcap);
  if(!om){ printf("memmap: owner map alloc failed\n"); free(pages); exit(1); }

  // Build char array for pages grouped by pages_per_block
  int blocks = (n + pages_per_block - 1) / pages_per_block;
  char *chars = malloc(blocks+1);
  if(!chars){ printf("memmap: chars alloc failed\n"); free(pages); free(om); exit(1); }

  for(int bi=0; bi<blocks; bi++){
    int start = bi * pages_per_block;
    int end = start + pages_per_block; if(end > n) end = n;
    // decide char by scanning pages in block
    char c = '.';
    for(int i=start;i<end;i++){
      unsigned char t = pages[i].type;
      if(t == PT_FREE) continue;
      if(t == PT_KERNEL){ c = '#'; break; }
      if(t == PT_USER){ c = get_symbol(pages[i].owner_pid, t); break; }
      if(t == PT_BCACHE) { c = 'B'; break; }
      if(t == PT_PAGETBL) { c = 'T'; break; }
      c = '?';
    }
    chars[bi] = c;
  }
  chars[blocks] = '\0';

  // Print map rows
  printf("memmap: %d pages, %d blocks (pages_per_block=%d)\n", n, blocks, pages_per_block);
  int rows = (blocks + width - 1) / width;
  for(int r=0;r<rows;r++){
    int row_start = r * width;
    int row_end = row_start + width; if(row_end > blocks) row_end = blocks;
    // print offset of first page in row
    printf("%06d: ", row_start * pages_per_block);
  for(int j=row_start;j<row_end;j++) printf("%c", chars[j]);
  printf("\n");
  }

  // Legend
  printf("\nLegend:\n");
  printf("  . = FREE (type=FREE)\n");
  printf("  # = KERNEL (type=KERNEL)\n");
  if(omn > 0) printf("  (owner symbols)\n");
  for(int i=0;i<omn;i++){
    printf("   %c = pid %d\n", om[i].sym, om[i].pid);
  }

  // Optionally, print counts per type
  int cnt_free=0,cnt_kernel=0,cnt_user=0,cnt_bcache=0,cnt_pagetbl=0,cnt_unknown=0;
  for(int i=0;i<n;i++){
    switch(pages[i].type){
      case PT_FREE: cnt_free++; break;
      case PT_KERNEL: cnt_kernel++; break;
      case PT_USER: cnt_user++; break;
      case PT_BCACHE: cnt_bcache++; break;
      case PT_PAGETBL: cnt_pagetbl++; break;
      default: cnt_unknown++; break;
    }
  }
  printf("\nCounts: FREE=%d KERNEL=%d USER=%d BCACHE=%d PAGETBL=%d UNKNOWN=%d\n", cnt_free,cnt_kernel,cnt_user,cnt_bcache,cnt_pagetbl,cnt_unknown);

  free(pages);
  free(om);
  free(chars);
  return 0;
}
