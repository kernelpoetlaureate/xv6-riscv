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
    // Phase 0: ROM and Bootloader
    // At power-on, RISC-V execution begins at physical address 0x0 in
    // machine mode. The firmware/ROM (QEMU's virt firmware on emulated
    // platforms) loads the xv6 kernel ELF image into DRAM at 0x80000000
    // and then transfers control to the kernel entry point `_entry`.
    // The physical region below 0x80000000 is reserved for platform
    // devices (UART, VIRTIO, PLIC, CLINT) and MMIO. This message
    // documents that early stage for traceability in the boot log.
    printf("[BOOT INFO] Phase 0: ROM and Bootloader (0x00000000 -> 0x80000000)\n");
    printf("[BOOT INFO] At power-on, execution starts at PA=0x0 in machine mode; firmware loads the kernel at 0x80000000 and jumps to _entry.\n");
  /* Professional boot banner: use reusable kbanner helper */
  kbanner("Welcome to xv6-riscv", "Minimal teaching OS — kernelpoetlaureate build");
    // Phase 3.1: Physical memory allocator (kinit)
    // Build the free-list of 4KB pages from the end of the kernel image
    // to PHYSTOP. Each freed page is added to a singly-linked freelist.
    printf("[BOOT INFO] Phase 3.1: kinit - initialize physical page allocator\n");
    printf("[BOOT INFO] This builds the kmem freelist (4KiB pages) from 'end' to PHYSTOP.\n");
    kinit();         // physical page allocator
    pageinfo_init(); // initialize page allocation tracking
    printf("[BOOT INFO] kinit completed. Free-list initialized and pageinfo ready.\n");

    // Phase 3.2: Kernel page table creation (kvmmake/kvminit)
    printf("[BOOT INFO] Phase 3.2: kvminit - create kernel page table and identity-map devices.\n");
    printf("[BOOT INFO] This allocates the kernel root page table and maps UART, VIRTIO, PLIC,\n");
    printf("           kernel text (read/exec), kernel data (read/write), and the TRAMPOLINE page.\n");
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    printf("[BOOT INFO] kvminit completed. Paging enabled with kernel page table.\n");

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
  /* Per-hart startup message: green for readiness */
  KLOG_INFO("hart %d: processor online\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }
  printf("[BOOT INFO] Entering scheduler. Kernel initialization complete; switching to process context.\n");
  scheduler();        
  scheduler();        
  return;
}
