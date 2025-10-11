#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// Note: direct VGA memory at 0xB8000 is x86-specific and not present
// on the RISC-V environment used by xv6. Use console `printf` instead.

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("Hello, World!\n");
    kinit();         // physical page allocator
    pageinfo_init(); // initialize page allocation tracking
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
  userinit();      // first user process
  // One-shot test: dump process info once at boot to verify the printer.
  // Disabled by default. To enable, define ENABLE_PROCESS_INSPECTOR_BOOT_DUMP
  // (for example, add -DENABLE_PROCESS_INSPECTOR_BOOT_DUMP to CFLAGS).
#ifdef ENABLE_PROCESS_INSPECTOR_BOOT_DUMP
  // This avoids creating a background kernel thread which can introduce
  // lock ordering complexities.
  dump_process_info();
#endif
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
  scheduler();        
  return;
}
