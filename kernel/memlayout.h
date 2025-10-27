/*
COMPILE TIME:
┌────────────────────────────────────────────────────────┐
│ 1. Compiler compiles start.c                          │
│    - Sees: char stack0[4096 * NCPU];                  │
│    - Allocates 32 KB in .bss section                  │
│                                                        │
│ 2. Linker (kernel.ld) runs                            │
│    - Places .text at 0x80000000                       │
│    - Places .rodata after .text                       │
│    - Places .data after .rodata                       │
│    - Places .bss after .data (includes stack0!)       │
│    - Creates kernel binary file                       │
│                                                        │
│ Result: kernel binary contains stack0 space           │
└────────────────────────────────────────────────────────┘

BOOT TIME (QEMU):
┌────────────────────────────────────────────────────────┐
│ 1. QEMU loads entire kernel binary to 0x80000000      │
│    - Loads .text section                              │
│    - Loads .rodata section                            │
│    - Loads .data section                              │
│    - Allocates .bss section (includes stack0!)        │
│                                                        │
│ 2. QEMU jumps to 0x80000000 (_entry)                 │
│                                                        │
│ Result: stack0 is ALREADY IN MEMORY                   │
│         at some address (say 0x80020000)              │
└────────────────────────────────────────────────────────┘

RUN TIME:
┌────────────────────────────────────────────────────────┐
│ entry.S FIRST INSTRUCTION:                            │
│    la sp, stack0                                      │
│                                                        │
│ This instruction means:                               │
│    "Load the ADDRESS of stack0 into sp"               │
│                                                        │
│ stack0 ALREADY EXISTS at this point!                  │
│ entry.S just points sp to it                          │
└────────────────────────────────────────────────────────┘
















*/



















// Physical memory layout
//consists of 3 regions:

// 1. Before 0x00000000 to 0x80000000 - hard coded, fixed in stone area (I/O devices)
                // 00001000 -- boot ROM, provided by qemu
                // 02000000 -- CLINT
                // 0C000000 -- PLIC
                // 10000000 -- uart0 
                // 10001000 -- virtio disk 
                // 80000000 -- qemu's boot ROM loads the kernel here,


/*2. 0x80000000 to 0x80023578 - Kernel space
with current settings, we can speculate that the kernel size is around 
0x23578 bytes
Size: 0x23578 bytes (144,760 bytes)
In KB: 141.37 KB
In MB: 0.14 MB
Page size: 4,096 bytes (4KB)
Number of pages: 35.34 pages (35 full pages)
the kernel sections are laid out as follows:
.text, .rodata, .data, .bss, 
*/ 

/*
kernel has 2 types of stacks:
1. A global stack used during boot (stack0)
2. Per-process kernel stacks used during process execution

the global stack is defined in .bss and used only for a short period :
1. entry.S (_entry) ← Uses stack0
2. start.c (start()) ← Uses stack0  
3. main.c (main())   ← Uses stack0

right after this stage of execution, kernel starts using per-process kernel stacks

*/



// qemu puts UART registers here in physical memory.
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio mmio interface
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// qemu puts platform-level interrupt controller (PLIC) here.
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)

// the kernel expects there to be RAM
// for use by the kernel and user pages
// from physical address 0x80000000 to PHYSTOP.
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024)

// map the trampoline page to the highest address,
// in both user and kernel space.
#define TRAMPOLINE (MAXVA - PGSIZE)

// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// User memory layout.
// Address zero first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
//   ...
//   TRAPFRAME (p->trapframe, used by the trampoline)
//   TRAMPOLINE (the same page as in the kernel)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)
