#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "pageinfo.h"

extern uint ticks;

// static compile-time number of pages covered
#define PI_NPAGES (PHYSTOP / PGSIZE)

static struct pageinfo pi_array[PI_NPAGES];
static struct spinlock pi_lock;

void
pageinfo_init(void)
{
  initlock(&pi_lock, "pageinfo_glob");
  for(int i = 0; i < PI_NPAGES; i++){
    pi_array[i].type = PGTYPE_FREE;
    pi_array[i].owner_pid = -1;
    pi_array[i].mapped_va = 0;
    pi_array[i].tag[0] = '\0';
    pi_array[i].alloc_tick = 0;
    pi_array[i].ref = 0;
  }
}

static struct pageinfo *pi_for_pa(void *pa)
{
  uint64 p = (uint64)pa;
  if(p % PGSIZE) return 0;
  uint64 idx = p / PGSIZE;
  if(idx >= PI_NPAGES) return 0;
  return &pi_array[idx];
}

void
pageinfo_set_alloc(void *pa, unsigned char type, int owner_pid, const char *tag)
{
  struct pageinfo *p = pi_for_pa(pa);
  if(!p) return;
  acquire(&pi_lock);
  p->type = type;
  p->owner_pid = owner_pid;
  p->alloc_tick = ticks; // Record current tick count for allocation time tracking
  p->ref = 1;
  if(tag){
    int i;
    for(i=0; i < PAGEINFO_TAGLEN-1 && tag[i]; i++) p->tag[i] = tag[i];
    p->tag[i] = '\0';
  }
  release(&pi_lock);
}

void
pageinfo_set_free(void *pa)
{
  struct pageinfo *p = pi_for_pa(pa);
  if(!p) return;
  acquire(&pi_lock);
  p->type = PGTYPE_FREE;
  p->owner_pid = -1;
  p->mapped_va = 0;
  p->tag[0] = '\0';
  p->alloc_tick = 0;
  p->ref = 0;
  release(&pi_lock);
}

void
pageinfo_set_mapped(void *pa, uint64 va, int owner_pid, unsigned char type)
{
  struct pageinfo *p = pi_for_pa(pa);
  if(!p) return;
  acquire(&pi_lock);
  p->mapped_va = va;
  p->owner_pid = owner_pid;
  p->type = type;
  p->ref++; // increment observed references
  release(&pi_lock);
}

void
pageinfo_print_all(void)
{
  for(uint64 i = 0; i < PI_NPAGES; i++){
    struct pageinfo *p = &pi_array[i];
    if(p->type != PGTYPE_FREE){
      printf("pa=0x%lx type=%d pid=%d va=0x%lx tag=%s ref=%lu\n",
        i*PGSIZE, p->type, p->owner_pid, p->mapped_va, p->tag, p->ref);
    }
  }
}

int
pageinfo_copy_to_user(uint64 dst, int max)
{
  if(max <= 0) return -1;
  int tocopy = (max < (int)PI_NPAGES) ? max : (int)PI_NPAGES;
  for(int i = 0; i < tocopy; i++){
    if(either_copyout(1, dst + i * sizeof(struct pageinfo), (char*)&pi_array[i], sizeof(struct pageinfo)) < 0)
      return -1;
  }
  return tocopy;
}

int
pageinfo_copy_entry_to_user(uint64 dst, void *pa)
{
  struct pageinfo *p = pi_for_pa(pa);
  if(!p) return -1;
  if(either_copyout(1, dst, (char*)p, sizeof(struct pageinfo)) < 0)
    return -1;
  return 0;
}
