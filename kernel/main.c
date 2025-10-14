#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// Reference to stack0 from start.c for boot logging
extern char stack0[];

// Note: direct VGA memory at 0xB8000 is x86-specific and not present
// on the RISC-V environment used by xv6. Use console `printf` instead.

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    // Boot sequence retrospective logging - document phases 0-2 that occurred before console init
    printf("[BOOT INFO] ===== xv6-riscv Boot Sequence =====\n");
    printf("[BOOT INFO] Phase 0: ROM and Bootloader (0x00000000 -> 0x80000000)\n");
    printf("[BOOT INFO] - Power-on: execution starts at PA=0x0 in machine mode\n");
    printf("[BOOT INFO] - Firmware loads kernel ELF at 0x80000000 and jumps to _entry\n");
    printf("[BOOT INFO] Phase 1: entry.S - Stack Setup (completed)\n");
    printf("[BOOT INFO] - Per-CPU stacks allocated from stack0[4096*NCPU]\n");
    printf("[BOOT INFO] - Hart %d: sp = stack0 + (%d * 4096) = 0x%lx\n", cpuid(), cpuid(), (uint64)&stack0[4096 * cpuid()]);
    printf("[BOOT INFO] - Paging DISABLED (satp=0), direct physical addressing\n");
    printf("[BOOT INFO] Phase 2: start.c - Machine Mode Configuration (completed)\n");
    printf("[BOOT INFO] - PMP: configured supervisor access to all physical memory\n");
    printf("[BOOT INFO] - Delegation: medeleg/mideleg -> supervisor mode handles traps\n");
    printf("[BOOT INFO] - Timer: mtime comparator programmed for interrupts\n");
    printf("[BOOT INFO] - Mode switch: M->S via mret, jumped to main()\n");
  /* Professional boot banner: use reusable kbanner helper */
  kbanner("Welcome to xv6-riscv", "Minimal teaching OS — kernelpoetlaureate build");
    // Phase 3.1: Physical memory allocator (kinit)
    // Build the free-list of 4KB pages from the end of the kernel image
    // to PHYSTOP. Each freed page is added to a singly-linked freelist.
    printf("[BOOT INFO] Phase 3.1: kinit - initialize physical page allocator\n");
    printf("[BOOT INFO] This builds the kmem freelist (4KiB pages) from 'end' to PHYSTOP.\n");
    extern char end[]; // defined by kernel.ld
    printf("[BOOT INFO] Range: 0x%lx to 0x%lx (~%ld MB available after kernel image)\n", 
           (uint64)end, PHYSTOP, (PHYSTOP - (uint64)end) / (1024*1024));
    kinit();         // physical page allocator
    pageinfo_init(); // initialize page allocation tracking
    printf("[BOOT INFO] kinit completed. Free-list initialized and pageinfo ready.\n");

    // Phase 3.2: Kernel page table creation (kvmmake/kvminit)
    printf("[BOOT INFO] Phase 3.2: kvminit - create kernel page table and identity-map devices.\n");
    printf("[BOOT INFO] This allocates the kernel root page table and maps UART, VIRTIO, PLIC,\n");
    printf("           kernel text (read/exec), kernel data (read/write), and the TRAMPOLINE page.\n");
    printf("[BOOT INFO] Critical: paging disabled until kvminithart() - CPU executes in physical mode\n");
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    printf("[BOOT INFO] kvminithart completed. Paging ENABLED - satp loaded, TLB flushed, virtual addressing active.\n");

    // Phase 3.3: Pre-allocate kernel stacks and process table
    printf("[BOOT INFO] Phase 3.3: procinit - initialize process table and pre-map kernel stacks.\n");
    printf("[BOOT INFO] This initializes struct proc[] for NPROC slots and sets each p->kstack = KSTACK(i).\n");
    procinit();      // process table
    printf("[BOOT INFO] procinit completed. proc[] entries initialized and kstack addresses assigned.\n");

    // Phase 3.4: Trap vectors
    printf("[BOOT INFO] Phase 3.4: trapinit - configure trap handlers and trampoline support.\n");
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    printf("[BOOT INFO] trapinit completed. Kernel trap vectors installed.\n");

    // Phase 3.5: Interrupt controller (PLIC)
    printf("[BOOT INFO] Phase 3.5: plicinit - initialize the platform-level interrupt controller.\n");
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    printf("[BOOT INFO] plicinit completed. Device interrupts configured.\n");

    // Phase 3.6: Filesystem and device init
    printf("[BOOT INFO] Phase 3.6: binit/iinit/fileinit/virtio_disk_init - buffer cache and FS setup.\n");
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    printf("[BOOT INFO] Filesystem and disk driver initialized.\n");
  userinit();      // first user process
  printf("[BOOT INFO] Phase 4: userinit - create first user process (PID 1) and prepare to run userland.\n");
  printf("[BOOT INFO] userinit sets up a minimal user page table, trapframe, and user stack for init.\n");
  printf("[BOOT INFO] PID 1 will execute initcode.S → exec('/init') → fork/exec('/bin/sh') for shell\n");
  // One-shot test: dump process info once at boot to verify the printer.
  // Disabled by default. To enable, define ENABLE_PROCESS_INSPECTOR_BOOT_DUMP
  // (for example, add -DENABLE_PROCESS_INSPECTOR_BOOT_DUMP to CFLAGS).
#ifdef ENABLE_PROCESS_INSPECTOR_BOOT_DUMP
  // This avoids creating a background kernel thread which can introduce
  // lock ordering complexities.
  dump_process_info();
#endif
    // Print boot summary only once from hart 0 after all init is complete
    printf("\n[BOOT COMPLETE] ===== xv6-riscv Boot Sequence Summary =====\n");
    printf("[BOOT COMPLETE] Phase 0: ROM (0x0) → loaded kernel at 0x80000000\n");
    printf("[BOOT COMPLETE] Phase 1: entry.S → per-CPU stacks, call start()\n");  
    printf("[BOOT COMPLETE] Phase 2: start.c → PMP config, M→S mode switch to main()\n");
    printf("[BOOT COMPLETE] Phase 3: main.c → kinit, kvminit, procinit, devices\n");
    printf("[BOOT COMPLETE] Phase 4: userinit → PID 1 allocated\n");
    printf("[BOOT COMPLETE] Phase 5: PID 1 user stack created\n");  
    printf("[BOOT COMPLETE] Phase 6: Shell (PID 2) ready for user interaction\n");
    printf("[BOOT COMPLETE] Critical invariants established:\n");
    printf("                - Physical memory allocator (freelist with SMP locking)\n");
    printf("                - Kernel page table with identity mappings + TRAMPOLINE\n");
    printf("                - 64 pre-allocated kernel stacks with guard pages\n");
    printf("                - Per-process virtual memory isolation via page tables\n");
    printf("[BOOT COMPLETE] Entering scheduler. Kernel→user transitions now possible.\n");
    printf("========================================================\n\n");
    
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
  /* Per-hart startup message: green for readiness */
  KLOG_INFO("hart %d: processor online\n", cpuid());
    printf("[SMP] Hart %d: secondary CPU online after hart 0 completed initialization\n", cpuid());
    printf("[SMP] Hart %d: enabling paging, installing trap vectors, configuring interrupts\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }
  
  scheduler();        
  return;
}
