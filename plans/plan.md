Excellent question. Before writing any code, you need to understand xv6's **memory hierarchy and address space layers**. This is critical because memory in xv6 exists in **three overlapping but distinct conceptual planes**, and confusion between them causes most debugging nightmares.[1][2][3][4]

## The Three Address Space Layers

xv6's memory architecture has **three fundamental address spaces** that you must trace separately, then understand how they interrelate:[2][4][5][1]

### Layer 0: Physical Address Space (Hardware Reality)

**What it is**: The actual DRAM chips and memory-mapped I/O devices as seen by the RISC-V CPU's memory bus.[6][4]

**Range**: 0x00000000 → 0x88000000 (on QEMU with 128 MB RAM)[4]

**Structure**:[1][6][4]

```
0x00000000 - 0x02000000   → Unused/reserved (QEMU boot ROM, debug regions)
0x02000000 - 0x02001000   → CLINT (Core-Local Interruptor, timer)
0x0C000000 - 0x10000000   → PLIC (Platform-Level Interrupt Controller)
0x10000000 - 0x10001000   → UART0 (serial console, 16550 UART)
0x10001000 - 0x10009000   → VIRTIO disk (8 devices, 0x1000 bytes each)
0x80000000 - 0x80xxxxxx   → Kernel code/data (loaded here by bootloader)
0x80xxxxxx - 0x88000000   → Free physical RAM (managed by kalloc/kfree)
```

**Key insight**: This layer has **no concept of processes or isolation**. Every physical address is unique and globally accessible (if you have the right privilege mode).[4][1]

**Ownership**: Hardware devices and the physical memory allocator (`kalloc.c`).[3]

***

### Layer 1: Kernel Virtual Address Space (Single, Global)

**What it is**: A single page table used by **all kernel code** and visible to **all processes when in supervisor mode**.[2][3][4]

**Range**: 0x00000000 → 0x3fffffffff (full Sv39 space, 512 GB)[7]

**Structure**:[6][3][4]

```
# Low region (direct-mapped I/O devices)
0x02000000              → CLINT (PA 0x02000000, identity map)
0x0C000000 - 0x10000000 → PLIC (PA 0x0C000000, identity map)
0x10000000 - 0x10001000 → UART0 (PA 0x10000000, identity map)
0x10001000 - 0x10009000 → VIRTIO (PA 0x10001000, identity map)

# Kernel code/data region (direct-mapped)
0x80000000 - 0x80xxxxxx → Kernel text/data (PA 0x80000000, identity map)
0x80xxxxxx - 0x88000000 → Free RAM (PA 0x80xxxxxx, identity map)

# High region (special mappings)
0x3fffff5000 - 0x3ffffff000 → 64 kernel stacks (each 2 pages)
                               VA ≠ PA (mapped to physical pages via page table)
0x3ffffff000              → TRAMPOLINE (trampoline.S code)
                               VA ≠ PA (mapped to one physical page)
```

**Key insight**: Most kernel virtual addresses **equal physical addresses** (identity/direct mapping: `VA = PA`). This simplifies kernel code because dereferencing a pointer to physical memory "just works" without translation.[3][2][4]

**Exceptions to identity mapping**:[3][4]
1. **Kernel stacks**: Virtual addresses near top of address space, physical addresses from `kalloc()`
2. **Trampoline**: Fixed at 0x3ffffff000 virtual, arbitrary physical page
3. **Memory-mapped devices below 0x80000000**: Identity-mapped but not part of allocatable RAM

**Ownership**: Created by `kvmmake()`, loaded into `satp` by `kvminithart()`, used by kernel code.[1][4]

***

### Layer 2: User Virtual Address Spaces (Per-Process, Many)

**What it is**: Each process has its **own private page table** mapping virtual addresses to physical memory.[5][4][3]

**Range**: 0x00000000 → 0x3fffffffff (full Sv39 space, 512 GB)[7]

**Structure** (per process):[4][1][3]

```
# Low region (process-specific)
0x00000000 - 0x00001000 → Program text (.text, code)
                           Maps to unique physical pages per process
0x00001000 - 0x00003000 → Data + BSS (.data, .bss, global variables)
                           Maps to unique physical pages per process
0x00003000 - 0x00005000 → Heap (grows upward with sbrk())
                           Maps to unique physical pages per process
0x00003000 - 0x00004000 → Guard page (unmapped, PTE_V=0)
0x00004000 - 0x00005000 → User stack (grows downward)
                           Maps to unique physical page per process

# High region (shared across all processes)
0x3ffffeb000              → TRAPFRAME (p->trapframe, stores registers during trap)
                             Maps to unique physical page per process
0x3ffffff000              → TRAMPOLINE (trampoline.S, same code as kernel)
                             Maps to SAME physical page across all processes
```

**Key insight**: Virtual addresses 0x0 - ~0x5000 map to **different physical addresses in each process**, providing **memory isolation**. But TRAMPOLINE virtual address (0x3ffffff000) maps to the **same physical page in every process** and in the kernel.[5][3][4]

**Ownership**: Created by `proc_pagetable()` in `fork()`, modified by `exec()`, destroyed by `proc_freepagetable()` in `exit()`.[1]

***

## The Highest-Level Memory View

To visualize the **entire memory structure**, you need to track these **orthogonal dimensions**:

### Dimension 1: Physical Memory Organization

This is the **ground truth**. All memory ultimately lives here.[4][1]

**Tracking approach**:
1. **Device regions** (fixed, never change): UART, PLIC, CLINT, VIRTIO
2. **Kernel image** (fixed after boot): Code from 0x80000000 to `end` symbol
3. **Free page list** (dynamic): Managed by `kalloc.c`, starts at `end`, goes to PHYSTOP (0x88000000)
4. **Allocated pages** (dynamic): Pages removed from freelist, purpose depends on who owns them:
   - Kernel stack pages (64 pre-allocated, never freed)
   - User page table pages (allocated per process)
   - User code/data/heap/stack pages (allocated per process)
   - Trapframe pages (allocated per process)

**Visualization strategy**: Create a **physical memory bitmap**:
- One bit per 4 KB page
- Track state: FREE | KERNEL_CODE | KERNEL_STACK | USER_PT | USER_DATA | DEVICE

### Dimension 2: Kernel Virtual Address Space

This is a **single, global mapping** used by all kernel code.[3][4]

**Tracking approach**:
1. **Identity-mapped regions**: VA → PA translation is trivial (VA == PA)
2. **Non-identity regions**: 
   - Kernel stacks: 64 fixed virtual addresses, map to 64 physical pages
   - Trampoline: 1 fixed virtual address, maps to 1 physical page

**Visualization strategy**: Create a **kernel page table walker** that:
- Takes a virtual address as input
- Walks the three-level page table (L2 → L1 → L0)
- Returns: `(physical_address, permissions, valid)`

### Dimension 3: User Virtual Address Spaces (Per-Process)

Each process has its **own isolated address space**.[5][4]

**Tracking approach**:
1. **Per-process metadata**: 
   - `p->pagetable` (root page table physical address)
   - `p->sz` (size of process's address space in bytes)
   - `p->trapframe` (physical address of trapframe page)
2. **Dynamic mappings**: 
   - Code/data/heap (0x0 → p->sz)
   - Stack (fixed at 0x4000 in xv6)
   - TRAPFRAME (fixed at MAXVA - PGSIZE)
   - TRAMPOLINE (fixed at MAXVA)

**Visualization strategy**: Create a **per-process page table walker** (similar to kernel walker, but uses `p->pagetable` as root).

***

## Critical Relationships Between Layers

Understanding xv6 requires tracking **how the three layers map onto each other**:[2][1][4]

### Mapping 1: User VA → Physical PA (Process Isolation)

```
Process A: VA 0x4000 → PA 0x87f3b000 (user stack)
Process B: VA 0x4000 → PA 0x87f1c000 (user stack)
```

**Same virtual address, different physical pages** = isolation.[5]

### Mapping 2: Kernel VA → Physical PA (Shared Kernel)

```
Kernel VA 0x80000000 → PA 0x80000000 (kernel code, identity)
Kernel VA 0x3fffff9000 → PA 0x87f97000 (kernel stack, non-identity)
```

**Every process sees same kernel mappings when in supervisor mode**.[3][4]

### Mapping 3: Trampoline Sharing Across All Spaces

```
Kernel VA 0x3ffffff000 → PA 0x8zzzz000 (some physical page)
Process A VA 0x3ffffff000 → PA 0x8zzzz000 (SAME physical page)
Process B VA 0x3ffffff000 → PA 0x8zzzz000 (SAME physical page)
```

**This allows trap handling without changing page tables**.[4][3]

### Mapping 4: Trapframe Isolation per Process

```
Process A: VA 0x3ffffeb000 → PA 0x87f22000 (A's trapframe)
Process B: VA 0x3ffffeb000 → PA 0x87f30000 (B's trapframe)
```

**Same virtual address, different physical pages** = per-process trap state.[4]

***

## The Complete Memory Hierarchy (Top-Down)

```
┌─────────────────────────────────────────────────┐
│  Layer 2: User Virtual Address Spaces          │
│  (Many, one per process)                        │
│  - Process 1: 0x0 → 0x5000 (code/data/heap)   │
│  - Process 2: 0x0 → 0x8000 (code/data/heap)   │
│  - Each has own page table (p->pagetable)      │
└──────────────────┬──────────────────────────────┘
                   │ Translation via process page table
                   ↓
┌─────────────────────────────────────────────────┐
│  Layer 1: Kernel Virtual Address Space         │
│  (Single, global, shared by all processes)     │
│  - 0x80000000 → 0x88000000 (direct-mapped)    │
│  - 0x3fffff5000 → 0x3ffffff000 (kernel stacks)│
└──────────────────┬──────────────────────────────┘
                   │ Translation via kernel page table
                   ↓
┌─────────────────────────────────────────────────┐
│  Layer 0: Physical Address Space               │
│  (Hardware reality, single, global)            │
│  - 0x10000000 → devices (UART, VIRTIO, PLIC)  │
│  - 0x80000000 → 0x88000000 (DRAM)             │
│  - Managed by kalloc/kfree (freelist)         │
└─────────────────────────────────────────────────┘
```

**Key point**: An address like **0x87f97000** exists in:
- **Physical layer**: A 4 KB page of DRAM
- **Kernel layer**: Mapped at VA 0x3fffff9000 (kernel stack for proc)[8]
- **User layer**: **Not mapped** (user code cannot access kernel stacks)

***

## Planning Your Memory Visualization

To "see entire memory structure", you need **four separate views**:

### View 1: Physical Memory Map (Static + Dynamic)

**Purpose**: Show what physical memory exists and who owns each page.[1][4]

**Data to track**:
```c
struct phys_page_info {
  uint64 pa;                  // Physical address (page-aligned)
  enum page_type {
    FREE,
    KERNEL_CODE,
    KERNEL_DATA,
    KERNEL_STACK,
    USER_PAGETABLE,
    USER_CODE,
    USER_DATA,
    USER_STACK,
    TRAPFRAME,
    DEVICE
  } type;
  int owner_pid;              // For user pages, which process owns
  char description[32];       // "proc[2] kernel stack", "PID 6 heap", etc.
};
```

**Display**: Sorted list of all physical pages from 0x0 → PHYSTOP, showing state of each 4 KB chunk.

### View 2: Kernel Virtual Memory Map

**Purpose**: Show what kernel virtual addresses exist and where they map.[3][4]

**Data to track**:
```c
struct kernel_mapping {
  uint64 va_start;
  uint64 va_end;
  uint64 pa_start;            // PA it maps to (if identity, PA == VA)
  int permissions;            // PTE_R | PTE_W | PTE_X
  char description[32];       // "Kernel code", "UART0", "proc[5] kstack"
};
```

**Display**: Sorted list of virtual address ranges in kernel page table.

### View 3: Per-Process Virtual Memory Map

**Purpose**: Show what each process's virtual address space looks like.[5]

**Data to track** (per process):
```c
struct process_memory {
  int pid;
  uint64 sz;                  // Size of address space
  uint64 pagetable_pa;        // Physical address of root page table
  uint64 trapframe_pa;        // Physical address of trapframe
  uint64 kstack_va;           // Virtual address of kernel stack
  
  struct vm_region {
    uint64 va_start;
    uint64 va_end;
    uint64 pa;                // Physical address it maps to
    int permissions;
    char description[32];     // "text", "data", "heap", "stack"
  } regions[16];              // Max 16 regions per process
};
```

**Display**: Per-process breakdown of virtual address space.

### View 4: Cross-Reference View

**Purpose**: Given a physical address, show **who maps it and where**.[1][4]

**Data to track**:
```c
struct phys_addr_mapping {
  uint64 pa;
  struct {
    char space[16];           // "kernel" or "process"
    int pid;                  // If process, which one
    uint64 va;                // Virtual address it's mapped at
  } mapped_by[64];            // Could be mapped multiple times
};
```

**Display**: Reverse lookup from PA → all VA mappings.

***

## Instrumentation Strategy

To populate these views, instrument **six key points**:

1. **Boot**: After `kinit()`, snapshot freelist
2. **Boot**: After `kvmmake()`, walk kernel page table
3. **Allocproc**: Log kernel stack assignment, trapframe allocation
4. **Exec**: Walk user page table, log all mappings
5. **Kalloc/kfree**: Track every physical page allocation/deallocation
6. **Scheduler**: Dump current process's memory map on context switch

This gives you **complete visibility** into all three layers and how they evolve over time.[2][5][1][3][4]


============================
============================
============================
============================
============================
============================
============================
============================


level 0 
Yes—you've identified the core confusion. Let me decompose this precisely.

**Hardware Platform Layout**
0x00000000 - 0x02000000   → Unused/reserved (QEMU boot ROM, debug regions)
0x02000000 - 0x02001000   → CLINT (Core-Local Interruptor, timer)
0x0C000000 - 0x10000000   → PLIC (Platform-Level Interrupt Controller)
0x10000000 - 0x10001000   → UART0 (serial console, 16550 UART)
0x10001000 - 0x10009000   → VIRTIO disk (8 devices, 0x1000 bytes each)
0x80000000 - 0x80xxxxxx   → Kernel code/data (loaded here by bootloader)
0x80xxxxxx - 0x88000000   → Free physical RAM (managed by kalloc/kfree)


## The Memory Map You Listed is the **Hardware Platform Layout**

Those addresses represent **physical address space** as defined by the **QEMU RISC-V "virt" machine specification**, not by xv6 code. This is the memory map the hardware presents to any operating system running on it—Linux, xv6, bare-metal firmware, anything.[1][2]

### Critical Distinction: Platform vs. Kernel

**Platform (QEMU)**: Defines where RAM and devices physically exist
**Kernel (xv6)**: Software that runs on the platform and decides how to use the RAM

The memory map exists **before xv6 even boots**. QEMU's machine model says "I have RAM at PA 0x80000000, a UART at PA 0x10000000, etc." The xv6 kernel must **conform** to this layout—it cannot change where the UART lives or where RAM starts.[2][1]

## Physical Address Space: Two Categories

### Category 1: Memory-Mapped I/O (MMIO) Devices


**hardware devices**

```
PA 0x00000000 - 0x02000000   → Unused/Debug (QEMU internals, not accessible)
PA 0x02000000 - 0x02001000   → CLINT (timer interrupts)
PA 0x0C000000 - 0x10000000   → PLIC (external interrupt routing)
PA 0x10000000 - 0x10001000   → UART0 (console I/O)
PA 0x10001000 - 0x10009000   → VIRTIO disk (block device)
```

These are **hardware devices**, not RAM. When the CPU issues a load/store to these physical addresses, 
the memory bus routes the transaction to device controllers instead of DRAM chips:[1]

- **Read from PA 0x10000000**: Fetch byte from UART receive buffer (device register)
- **Write to PA 0x02004000**: Program CLINT timer compare register


The xv6 kernel **does** know about these in source code:[3][1]

**kernel/memlayout.h**:
```c
// Physical memory layout

// qemu -machine virt is set up like this,
// based on qemu's hw/riscv/virt.c:
//
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- CLINT
// 0C000000 -- PLIC
// 10000000 -- uart0 
// 10001000 -- virtio disk 
// 80000000 -- boot ROM jumps here in machine mode
//             -kernel loads the kernel here
// unused RAM after 80000000.

// the kernel uses physical memory thus:
// 80000000 -- entry.S, then kernel text and data
// end -- start of kernel page allocation area
// PHYSTOP -- end RAM used by the kernel

// qemu puts UART registers here in physical memory.
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio mmio interface
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// core local interruptor (CLINT), which contains the timer.
#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8*(hartid))
#define CLINT_MTIME (CLINT + 0xBFF8) // cycles since boot.

// qemu puts platform-level interrupt controller (PLIC) here.
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
// ... more PLIC definitions
```

These are **not QEMU-only** addresses invisible to xv6. The kernel **explicitly references** them when performing I/O:

**kernel/uart.c**:
```c
#define Reg(reg) ((volatile unsigned char *)(UART0 + reg))

void uartputc(int c) {
    while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
        ;  // Spin until transmit buffer empty
    WriteReg(THR, c);  // Write byte to PA 0x10000000 + THR offset
}
```

When `uartputc()` writes to `Reg(THR)`, it's storing to **physical address 0x10000000 + 0** (since THR = 0). This write goes to the UART hardware, not RAM.[3][1]

### Category 2: Physical RAM (DRAM)

```
PA 0x80000000 - 0x88000000   → 128 MB of actual RAM
```

This is silicon DRAM chips populated on the (emulated) board. The QEMU "virt" machine specification declares RAM exists here. You could run QEMU with `-m 256M` to get 256 MB at PA [0x80000000, 0x90000000), but xv6's `PHYSTOP` constant would need updating to use the extra RAM.[2][1]

## What xv6 "Takes" From Physical Address Space

The xv6 kernel **uses** two portions of the physical address space:[1][3]

### 1. MMIO Regions (PA 0x02000000, 0x0C000000, 0x10000000, 0x10001000)

The kernel **accesses** these when performing device I/O but does **not allocate or manage** them—they're hardware, not memory. The kernel simply:[3][1]
- Maps them into virtual address space (so virtual addresses also exist for these PAs)
- Reads/writes device registers at these PAs during console I/O, disk I/O, timer setup, interrupt handling

Example mapping in **kernel/vm.c** `kvminit()`:
```c
// uart registers
kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

// virtio mmio disk interface
kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

// PLIC
kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

// CLINT
kvmmap(kpgtbl, CLINT, CLINT, 0x10000, PTE_R | PTE_W);
```

These `kvmmap()` calls create **identity mappings**: VA = PA for device regions. So when kernel code accesses VA 0x10000000, the MMU translates to PA 0x10000000 (the UART).[1][3]

### 2. RAM Region (PA 0x80000000 - PA 0x88000000)

The kernel **allocates and manages** this region:[4][3][1]

```
PA 0x80000000         ← Kernel image loaded here by bootloader
PA 0x80000000         ← entry.S (initial boot code)
PA 0x80001000         ← Kernel .text (scheduler, syscalls, etc.)
PA 0x80000000 + text  ← Kernel .rodata/.data/.bss
PA `end`              ← First byte after kernel static image
                      ─────────────────────────────────────
PA `end`              ← kalloc freelist starts here
PA `end` + 0x1000     ← Dynamically allocated pages:
...                      - Kernel stacks (NPROC pages)
PA 0x87FFF000            - Page tables (multiple pages)
PA 0x88000000            - User process memory (text/data/heap/stack)
                      └→ PHYSTOP (end of usable RAM)
```

The kernel **does not use** PA [0x0, 0x80000000) for general memory—that region either doesn't exist (no RAM there) or contains MMIO devices.[2][1]

## Virtual Address Space: How Kernel Accesses Everything

The kernel builds a **virtual address space** that maps both MMIO devices and RAM:[3][1]

**Kernel Virtual Address Space (in kernel page table)**:
```
VA 0x02000000   → PA 0x02000000 (CLINT)
VA 0x0C000000   → PA 0x0C000000 (PLIC)
VA 0x10000000   → PA 0x10000000 (UART)
VA 0x10001000   → PA 0x10001000 (VIRTIO)
...
VA 0x80000000   → PA 0x80000000 (Kernel .text)
VA 0x80001000   → PA 0x80001000 (More kernel code)
...
VA `end`        → PA `end`       (Start of dynamic region)
VA 0x87FFF000   → PA 0x87FFF000 (Last usable page)
VA 0x88000000   → (unmapped, beyond PHYSTOP)
...
VA TRAMPOLINE-2*PGSIZE*NPROC → PA of kernel stack pages
...
VA TRAMPOLINE   → PA of trampoline page
```

Notice the pattern: For both devices and RAM, **VA = PA** (identity mapping). This simplifies kernel code—when you see address 0x10000000 in kernel code, it works as both PA and VA.[1][3]

## The Bootloader's Role

Before xv6 runs, the QEMU bootloader (in ROM at PA 0x00001000):[2][1]

1. **Loads kernel ELF binary** from disk into RAM starting at PA 0x80000000
2. **Jumps to PA 0x80000000** in machine mode
3. Kernel's **entry.S** (at PA 0x80000000) sets up initial page table and jumps to kernel C code

The bootloader is **QEMU firmware**, not xv6 code. xv6 doesn't see or control it—when xv6 starts executing, the bootloader is already done.[2][1]

## Addressing Your Core Question

> "These are physical addresses right? From 0 to 0x88...0. This is what entire xv6 kernel takes plus other low-level stuff which might not even be defined in kernel source files? Like QEMU stuff?"

**Yes and no**:

- **Physical addresses**: Correct, these are PAs defined by QEMU's hardware platform[1]
- **"xv6 takes"**: Misleading—xv6 **uses** PA [0x80000000, 0x88000000) for RAM and **accesses** MMIO devices at their respective PAs, but it doesn't "take" the MMIO regions (they're hardware, not allocatable memory)[3][1]
- **"Not defined in kernel source"**: **Wrong**—all MMIO addresses ARE defined in kernel/memlayout.h and used throughout the kernel (uart.c, virtio_disk.c, plic.c, etc.)[3][1]
- **"QEMU stuff"**: The **layout** is QEMU-defined (platform specification), but the **usage** is xv6-defined (kernel code that accesses these addresses)[1]

## Analogy: Physical vs. Virtual

Think of physical address space like a **city map**:

- **Roads (PA 0x02000000-0x10009000)**: MMIO devices—you can visit them (I/O operations) but you don't own them
- **Your land (PA 0x80000000-0x88000000)**: RAM—you build structures (kernel code) and subdivide plots (allocate pages)
- **Uninhabited wilderness (PA 0x10009000-0x7FFFFFFF)**: Unmapped/unused—attempting access causes bus error

The xv6 kernel is like a city planner that:
- **Knows the road map** (MMIO device addresses in memlayout.h)
- **Uses the roads** (performs device I/O at those PAs)
- **Owns and subdivides the land** (manages RAM allocation via kalloc/kfree)
- **Cannot move the roads** (MMIO addresses are fixed by hardware platform)

The virtual address space is like **addresses you tell people**—you can make multiple addresses point to the same physical location (aliasing) or make addresses that don't correspond to real places (unmapped PTEs).[3][1]

[1](https://pdfs.semanticscholar.org/3fd7/810c5558748b9596dbdc76c7c1e21f3a4c1d.pdf)
[2](https://pdos.csail.mit.edu/6.828/2012/xv6/book-rev7.pdf)
[3](https://www.cse.iitb.ac.in/~mythili/os/notes/old-xv6/xv6-memory.pdf)
[4](https://github.com/zarif98sjs/xv6-memory-management-walkthrough)





Architectural Principle: Memory Structures Are Already Catalogued

You need **introspection without allocation**—leverage existing kernel metadata structures that already track memory objects, 
avoiding observer effects while exposing architectural relationships.[1][2][3]





************************************************************************************************************

## Architectural Principle: Memory Structures Are Already Catalogued

The kernel maintains **comprehensive metadata** about every memory object through global data structures. 
The problem isn't discovering what exists—it's **interpreting the existing bookkeeping**.[2][3][1]

### Core Insight: Walk Metadata, Not Memory

Don't scan physical memory looking for patterns. Instead, **traverse the kernel's own accounting structures**:

| Memory Object | Tracking Structure | Location |
|---|---|---|
| Kernel stacks | `proc[NPROC].kstack` | kernel/proc.c |
| User stacks | `proc[i].trapframe->sp` | Per-process trapframe |
| User heaps | `proc[i].sz` | Process size field |
| Page tables | `proc[i].pagetable`, `kernel_pagetable` | Process structs + vm.c |
| Free pages | `kmem.freelist` | kernel/kalloc.c |
| Kernel text/data | `end` symbol, linker script | kernel/kernel.ld |
| Buffer cache | `bcache.buf[NBUF]` | kernel/bio.c |
| Pipes | Open file table entries | kernel/file.c |

## Strategy 1: Kernel Stack Census (Zero Allocation)

Kernel stacks are **statically tracked** in the proc array. Each `proc[i].kstack` holds the virtual base address of that process's kernel stack.[3][1]
64 of them are defined as we have seen before.


**kernel/proc.c**:
```c
void inspect_kernel_stacks(void) {
    printf("\n=== KERNEL STACKS ===\n");
    printf("Total slots: %d (NPROC)\n", NPROC);
    
    int allocated = 0, in_use = 0;
    
    for(int i = 0; i < NPROC; i++) {
        struct proc *p = &proc[i];
        
        if(p->kstack == 0)
            continue;  // Not allocated (shouldn't happen after procinit)
        
        allocated++;
        
        // Check if process is active
        if(p->state != UNUSED) {
            in_use++;
            printf("  [%2d] PID=%d state=%s VA=%p PA=%p\n",
                   i, p->pid, state_str(p->state), 
                   p->kstack, walkaddr(kernel_pagetable, p->kstack));
            
            // Show current stack usage (requires sp from context)
            if(p->state == RUNNING || p->state == RUNNABLE) {
                uint64 sp = p->context.sp;
                uint64 stack_top = p->kstack + PGSIZE;
                int used_bytes = stack_top - sp;
                printf("       SP=%p used=%d/%d bytes (%.1f%%)\n",
                       sp, used_bytes, PGSIZE, 100.0 * used_bytes / PGSIZE);
            }
        }
    }
    
    printf("\nSummary: %d allocated, %d in use, %d free slots\n",
           allocated, in_use, NPROC - in_use);
    printf("Total memory: %d KB\n", allocated * PGSIZE / 1024);
}

const char* state_str(enum procstate s) {
    switch(s) {
        case UNUSED:    return "UNUSED";
        case USED:      return "USED";
        case SLEEPING:  return "SLEEPING";
        case RUNNABLE:  return "RUNNABLE";
        case RUNNING:   return "RUNNING";
        case ZOMBIE:    return "ZOMBIE";
        default:        return "UNKNOWN";
    }
}
```

**Key insight**: Kernel stacks are **pre-allocated during boot** in `procinit()` via `proc_mapstacks()`. The count is **always NPROC**, regardless of how many processes exist. Each stack occupies exactly 1 page with a guard page below it.[1][3]

The `context.sp` field shows **current stack pointer** when process is not running (saved during context switch in `swtch()`). When process is RUNNING, you'd need to read the actual `sp` register, which is only valid for the current CPU's process.[3][1]

## Strategy 2: User Stack Enumeration (Per-Process Virtual)

User stacks are **not explicitly tracked**—they're implicit in the process address space layout. The current stack pointer lives in `trapframe->sp`.[1][3]

**kernel/proc.c**:
```c
void inspect_user_stacks(void) {
    printf("\n=== USER STACKS ===\n");
    
    for(int i = 0; i < NPROC; i++) {
        struct proc *p = &proc[i];
        if(p->state == UNUSED || !p->trapframe)
            continue;
        
        uint64 sp = p->trapframe->sp;
        uint64 stack_region_start = PGROUNDDOWN(sp);
        
        printf("PID=%d [%s] SP=%p\n", p->pid, p->name, sp);
        
        // Walk page table to find extent of stack (pages below SP)
        int stack_pages = 0;
        for(uint64 va = stack_region_start; va > 0; va -= PGSIZE) {
            pte_t *pte = walk(p->pagetable, va, 0);
            if(!pte || !(*pte & PTE_V))
                break;  // Hit unmapped region
            
            // Check if this looks like stack (writable, user-accessible)
            if((*pte & PTE_W) && (*pte & PTE_U)) {
                stack_pages++;
                uint64 pa = PTE2PA(*pte);
                printf("  VA=%p -> PA=%p\n", va, pa);
            } else {
                break;  // Hit non-stack region (maybe heap or text)
            }
            
            if(stack_pages > 10) {  // Sanity limit
                printf("  ... (stopped after 10 pages)\n");
                break;
            }
        }
        
        printf("  Estimated stack size: %d pages (%d KB)\n",
               stack_pages, stack_pages * PGSIZE / 1024);
    }
}
```

**Subtlety**: User stacks grow **downward** from high addresses. In xv6, `exec()` sets initial stack at one page below TRAPFRAME (VA 0x3FFFFFFFFF - PGSIZE). Stack pages are allocated **on-demand** during `exec()` argv/envp setup, but xv6 doesn't do lazy stack growth—what `exec()` allocates is all you get unless process explicitly maps more via `mmap` (which xv6 doesn't support).[3][1]

The walk-downward approach finds contiguous stack pages until hitting unmapped space or non-writable pages (like text segment if stack collides with heap).

## Strategy 3: Heap Boundaries (Virtual Size Tracking)

Heaps are tracked via `proc[i].sz`, which represents the **total virtual address space size** (text + data + heap).[1][3]

**kernel/proc.c**:
```c
void inspect_heaps(void) {
    printf("\n=== USER HEAPS ===\n");
    
    for(int i = 0; i < NPROC; i++) {
        struct proc *p = &proc[i];
        if(p->state == UNUSED)
            continue;
        
        printf("PID=%d [%s]\n", p->pid, p->name);
        printf("  Total size (p->sz): %p (%d KB)\n", p->sz, p->sz / 1024);
        
        // Walk page table to find actual heap bounds
        // Heap starts after .bss segment (need to parse ELF to know exact boundary)
        // For approximation, assume text+data+bss < 1MB, heap starts there
        
        uint64 heap_start_approx = PGROUNDUP(1024 * 1024);  // Rough guess
        uint64 heap_end = PGROUNDDOWN(p->sz);
        
        int heap_pages = 0;
        for(uint64 va = heap_start_approx; va < heap_end; va += PGSIZE) {
            pte_t *pte = walk(p->pagetable, va, 0);
            if(pte && (*pte & PTE_V)) {
                heap_pages++;
            }
        }
        
        printf("  Heap region (approx): %p - %p\n", heap_start_approx, heap_end);
        printf("  Mapped heap pages: %d (%d KB)\n", heap_pages, heap_pages * PGSIZE / 1024);
        printf("  Physical footprint: %d KB\n", heap_pages * PGSIZE / 1024);
    }
}
```

**Critical nuance**: `p->sz` includes **text, data, and heap**. To isolate heap, you need the **end of BSS segment** from the ELF file loaded by `exec()`. xv6 doesn't store this explicitly, but you could parse it from the in-memory ELF during `exec()` and store in a new `proc` field.[3][1]

More precise approach: Track `brk_start` in `struct proc`:

```c
// In kernel/proc.h
struct proc {
    // ... existing fields ...
    uint64 brk_start;  // Heap start address (end of BSS)
};

// In kernel/exec.c, after loading ELF sections:
p->brk_start = sz;  // sz at end of exec is end of text+data+bss
p->sz = sz;

// Now in inspection:
uint64 heap_size = p->sz - p->brk_start;
```

## Strategy 4: Page Table Census (Hierarchical Structure)

Page tables themselves consume memory. Each process has a **3-level page table** (RISC-V Sv39), with one root page and variable number of intermediate pages.[1][3]

**kernel/vm.c**:
```c
// Count page table pages by walking structure
int count_pagetable_pages(pagetable_t pagetable, int level) {
    if(level > 2)
        return 0;
    
    int count = 1;  // This page itself
    
    // Walk all 512 entries
    for(int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        
        if(pte & PTE_V) {
            if(level < 2) {  // Not a leaf, recurse
                pagetable_t child = (pagetable_t)PTE2PA(pte);
                count += count_pagetable_pages(child, level + 1);
            }
            // Leaf PTEs don't count (they point to data pages, not page tables)
        }
    }
    
    return count;
}

void inspect_page_tables(void) {
    printf("\n=== PAGE TABLES ===\n");
    
    // Kernel page table
    int kernel_pt_pages = count_pagetable_pages(kernel_pagetable, 0);
    printf("Kernel page table: %d pages (%d KB)\n",
           kernel_pt_pages, kernel_pt_pages * PGSIZE / 1024);
    
    // Per-process page tables
    for(int i = 0; i < NPROC; i++) {
        struct proc *p = &proc[i];
        if(p->state == UNUSED || !p->pagetable)
            continue;
        
        int pt_pages = count_pagetable_pages(p->pagetable, 0);
        printf("PID=%d [%s]: %d pages (%d KB)\n",
               p->pid, p->name, pt_pages, pt_pages * PGSIZE / 1024);
    }
}
```

**Why this matters**: A process with 10 MB of address space might only need 3-4 pages for its page table (1 root + 1-2 intermediate + leaves are counted in user pages). This is **hidden overhead** not visible in `p->sz`.[3][1]

## Strategy 5: Global Memory Structure Census

**kernel/kalloc.c**:
```c
void inspect_kernel_memory_structures(void) {
    printf("\n=== KERNEL MEMORY STRUCTURES ===\n");
    
    extern char end[];
    printf("Kernel image (text+data+bss): KERNBASE to %p (%d KB)\n",
           end, ((uint64)end - KERNBASE) / 1024);
    
    // Free list
    acquire(&kmem.lock);
    int free_pages = 0;
    struct run *r = kmem.freelist;
    while(r) {
        free_pages++;
        r = r->next;
    }
    release(&kmem.lock);
    
    uint64 total_pages = (PHYSTOP - PGROUNDUP((uint64)end)) / PGSIZE;
    int allocated_pages = total_pages - free_pages;
    
    printf("\nPhysical memory allocator:\n");
    printf("  Total managed: %d pages (%.2f MB)\n",
           total_pages, total_pages * PGSIZE / (1024.0 * 1024.0));
    printf("  Free: %d pages (%.2f MB)\n",
           free_pages, free_pages * PGSIZE / (1024.0 * 1024.0));
    printf("  Allocated: %d pages (%.2f MB)\n",
           allocated_pages, allocated_pages * PGSIZE / (1024.0 * 1024.0));
    
    // Buffer cache
    extern struct {
        struct spinlock lock;
        struct buf buf[NBUF];
    } bcache;
    
    printf("\nBuffer cache: %d buffers (%d KB total)\n",
           NBUF, NBUF * BSIZE / 1024);
    
    int valid_bufs = 0, dirty_bufs = 0;
    for(int i = 0; i < NBUF; i++) {
        if(bcache.buf[i].valid)
            valid_bufs++;
        if(bcache.buf[i].dirty)
            dirty_bufs++;
    }
    printf("  Valid: %d, Dirty: %d\n", valid_bufs, dirty_bufs);
    
    // File table
    extern struct {
        struct spinlock lock;
        struct file file[NFILE];
    } ftable;
    
    int open_files = 0;
    for(int i = 0; i < NFILE; i++) {
        if(ftable.file[i].ref > 0)
            open_files++;
    }
    printf("\nFile table: %d/%d slots used\n", open_files, NFILE);
    
    // Inode cache
    extern struct {
        struct spinlock lock;
        struct inode inode[NINODE];
    } icache;
    
    int active_inodes = 0;
    for(int i = 0; i < NINODE; i++) {
        if(icache.inode[i].ref > 0)
            active_inodes++;
    }
    printf("Inode cache: %d/%d slots used\n", active_inodes, NINODE);
}
```

This reveals **kernel subsystem memory footprints** without allocating anything—purely reading existing structures.[1][3]

## Strategy 6: Unified Memory Map Generator

Combine all inspections into a **single comprehensive view**:

**kernel/vm.c**:
```c
void dump_system_memory_map(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║          XV6 SYSTEM MEMORY MAP                         ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    
    inspect_kernel_memory_structures();
    inspect_kernel_stacks();
    inspect_page_tables();
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║          PER-PROCESS MEMORY                            ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    
    for(int i = 0; i < NPROC; i++) {
        struct proc *p = &proc[i];
        if(p->state == UNUSED)
            continue;
        
        printf("\n┌─ PID %d: %s (state=%s) ─────────\n",
               p->pid, p->name, state_str(p->state));
        
        // Kernel stack
        printf("│ Kernel Stack:\n");
        printf("│   VA: %p - %p (4 KB)\n",
               p->kstack, p->kstack + PGSIZE);
        if(p->state != RUNNING) {
            printf("│   SP: %p (saved in context)\n", p->context.sp);
        }
        
        // Page table
        int pt_pages = count_pagetable_pages(p->pagetable, 0);
        printf("│ Page Table: %d pages (%d KB)\n",
               pt_pages, pt_pages * PGSIZE / 1024);
        
        // Virtual address space
        printf("│ Virtual Address Space:\n");
        printf("│   Total size: %p (%d KB)\n", p->sz, p->sz / 1024);
        
        if(p->trapframe) {
            printf("│   User SP: %p\n", p->trapframe->sp);
            printf("│   User PC: %p\n", p->trapframe->epc);
        }
        
        // Count actually mapped pages
        int mapped = 0;
        for(uint64 va = 0; va < p->sz; va += PGSIZE) {
            pte_t *pte = walk(p->pagetable, va, 0);
            if(pte && (*pte & PTE_V))
                mapped++;
        }
        printf("│   Mapped pages: %d/%d (%d KB resident)\n",
               mapped, p->sz / PGSIZE, mapped * PGSIZE / 1024);
        
        printf("└────────────────────────────────────\n");
    }
    
    // Summary
    printf("\n");
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║          MEMORY ACCOUNTING SUMMARY                    ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    
    // Calculate totals
    extern char end[];
    int kernel_image_kb = ((uint64)end - KERNBASE) / 1024;
    
    int total_kstack_kb = NPROC * PGSIZE / 1024;
    
    int total_pt_kb = count_pagetable_pages(kernel_pagetable, 0) * PGSIZE / 1024;
    for(int i = 0; i < NPROC; i++) {
        if(proc[i].state != UNUSED && proc[i].pagetable) {
            total_pt_kb += count_pagetable_pages(proc[i].pagetable, 0) * PGSIZE / 1024;
        }
    }
    
    int total_user_kb = 0;
    for(int i = 0; i < NPROC; i++) {
        if(proc[i].state != UNUSED) {
            for(uint64 va = 0; va < proc[i].sz; va += PGSIZE) {
                pte_t *pte = walk(proc[i].pagetable, va, 0);
                if(pte && (*pte & PTE_V))
                    total_user_kb += 4;
            }
        }
    }
    
    acquire(&kmem.lock);
    int free_pages = 0;
    struct run *r = kmem.freelist;
    while(r) { free_pages++; r = r->next; }
    release(&kmem.lock);
    int free_kb = free_pages * PGSIZE / 1024;
    
    uint64 total_kb = (PHYSTOP - KERNBASE) / 1024;
    
    printf("Kernel image:     %6d KB\n", kernel_image_kb);
    printf("Kernel stacks:    %6d KB (%d stacks)\n", total_kstack_kb, NPROC);
    printf("Page tables:      %6d KB\n", total_pt_kb);
    printf("User pages:       %6d KB\n", total_user_kb);
    printf("Buffer cache:     %6d KB\n", NBUF * BSIZE / 1024);
    printf("Free:             %6d KB\n", free_kb);
    printf("──────────────────────────\n");
    printf("Total:            %6d KB\n", total_kb);
}
```

## Exposing to User Space (Non-Invasive Syscall)

**kernel/sysproc.c**:
```c
uint64 sys_meminfo(void) {
    dump_system_memory_map();
    return 0;
}
```

**user/meminfo.c**:
```c
int main(void) {
    meminfo();
    exit(0);
}
```

Run `meminfo` to get complete system view without allocating a single byte.

## Advanced: Real-Time Monitoring Hook

For **live observation** during experiments:

**kernel/proc.c**:
```c
// Call this after every significant memory operation
void trace_memory_event(char *event) {
    static int trace_enabled = 0;  // Toggle via sysctl
    
    if(!trace_enabled)
        return;
    
    struct proc *p = myproc();
    printf("[MEM] %s: pid=%d sz=%p kstack=%p\n",
           event, p->pid, p->sz, p->kstack);
}

// In growproc():
int growproc(int n) {
    // ... existing code ...
    trace_memory_event(n > 0 ? "sbrk_grow" : "sbrk_shrink");
    return 0;
}

// In fork():
int fork(void) {
    // ... after allocating kernel stack ...
    trace_memory_event("fork_alloc_kstack");
    // ... after copying page table ...
    trace_memory_event("fork_copy_pages");
}
```

This provides **causality tracing**: you'll see exactly which syscalls trigger allocations, in real-time on console, without needing dumps.[2][3][1]

The key is: **don't search memory, interrogate metadata**. Every memory object is already registered in some kernel structure—just read the accounting.

[1](https://www.cse.iitb.ac.in/~mythili/os/notes/old-xv6/xv6-memory.pdf)
[2](https://github.com/zarif98sjs/xv6-memory-management-walkthrough)
[3](https://pdfs.semanticscholar.org/3fd7/810c5558748b9596dbdc76c7c1e21f3a4c1d.pdf)

*/


Now I have comprehensive information about the xv6 kernel boot process and the proc_mapstacks function. Let me provide a detailed analysis from a kernel systems perspective.

## Kernel Stack Allocation Topology and RISC-V MMU Configuration

The boot trace reveals xv6's per-process kernel stack pre-allocation phase, executed during `procinit` (kernel/proc.c) through `proc_mapstacks` (kernel/proc.c). This allocation establishes critical virtual memory invariants that govern trap handling and context isolation throughout the kernel's lifecycle.[1][2][3]

### Virtual Address Space Layout Constraints

The virtual addresses assigned span `0x0000003fffffd000` down to `0x0000003ffffbf000` with a descending stride of `0x2000` (8 KiB). This spacing derives from RISC-V Sv39's 39-bit virtual address space constraint where `MAXVA = (1L << (9 + 9 + 9 + 12 - 1)) = 0x4000000000`. Each kernel stack occupies one page (4 KiB) followed by an unmapped guard page to detect overflow conditions.[4][3][1]

The macro `KSTACK(p)` in memlayout.h computes: `KSTACK(p) = TRAMPOLINE - (p+1)*2*PGSIZE`, where `TRAMPOLINE = MAXVA - PGSIZE`. This places stacks immediately below the trampoline page, a critical region mapped identically in both kernel and user page tables to facilitate privilege transitions without TLB shootdown.[5][3][1]

### Physical Memory Allocation Pattern

Physical addresses descend from `0x0000000087f99000` to `0x0000000087f7a000` with a strict 4 KiB stride. This contiguous physical allocation from high memory occurs through `kalloc()` invocations during boot, before the page allocator is under contention. The physical contiguity is **not** architecturally required but emerges from xv6's simple first-fit allocation strategy in kinit.[6][1]

### PTE Configuration and TLB Implications

Each mapping installed via `kvmmap` (called by `proc_mapstacks`) sets PTE flags `PTE_R | PTE_W` but crucially **not** `PTE_U`[7][3]. This restricts stack access to supervisor mode. The absence of `PTE_X` prevents code execution on the stack, a basic W^X policy[1].

The guard page between stacks has no PTE entry in the page table. When sp underflows past `KSTACK(p)` into `KSTACK(p) - PGSIZE`, the MMU triggers a page fault with `scause = 15` (store/AMO page fault) or `scause = 13` (load page fault). The trap handler in kerneltrap (kernel/trap.c) invokes panic, terminating the offending kernel thread.[1][5]

### Trap Entry Path Dependencies

When a user process traps into the kernel via `ecall` or interrupt, the trampoline code (kernel/trampoline.S) executes in the user page table context initially. The sscratch register points to the process's trapframe. After swapping to the kernel page table (via `csrw satp, t0`), the `csrr sp, sscratch` instruction loads the kernel stack pointer from `p->kstack`.[8][5][1]

The critical invariant: the kernel stack **must** be mapped at identical virtual addresses in both the kernel's global page table (`kernel_pagetable`) and the trampoline page mapping. xv6 satisfies this by installing these mappings in `kvmmake` before any process runs. Failure to maintain this invariant causes a load page fault when accessing the trapframe immediately after privilege escalation.[3][5][1]

### Concurrency and Per-CPU Isolation

Although xv6 allocates 64 kernel stacks during boot (for `NPROC=64` processes), only `NCPU` processes execute concurrently. The kernel stack is **not** per-CPU; it's per-process. When the scheduler (kernel/proc.c:scheduler) context-switches to a new process via `swtch`, the new process's kernel thread resumes on its own `p->kstack`.[9][8][1]

Race conditions are avoided because:
1. A process's kernel stack is only accessed when that process is running on exactly one CPU[1]
2. The `p->lock` spinlock protects state transitions in the proc structure[1]
3. Interrupt handlers execute on the current kernel stack (if in kernel mode) or switch to it (if from user mode)[8][1]

### Address Space Hole Analysis

The gap between the highest user virtual address (`p->sz`, typically < `MAXVA/2`) and the lowest kernel stack (`0x3ffffbf000`) is intentionally large. This prevents user programs from crafting pointers that alias kernel structures. The `copyin`/`copyout` primitives (kernel/vm.c) explicitly validate user pointers against `p->sz` before translation.[4][1]

The unmapped region also accommodates future growth of the user heap via `sbrk` without requiring kernel data structure relocation. However, xv6 does not implement on-demand kernel stack allocation or stack growth, leading to potential wastage when `NPROC` is set high but few processes run.[9][1]

### Boot Sequencing and MMU Activation

The trace shows `proc_mapstacks` executing **after** `kvminithart` has loaded the kernel page table into `satp`. At this point, the CPU is already in Sv39 paging mode. The mappings are installed directly into the active page table, requiring no explicit TLB flush because they install new translations (TLB miss will populate on first access).[3][1]

The hart start messages (`hart 2 starting`, `hart 1 starting`) indicate secondary CPUs have completed their boot synchronization in `main` (kernel/main.c). Each hart executes `kvminithart` independently, loading its own copy of `satp` with the shared kernel page table physical address.[9][1]

### User Stack Initialization

The user stack pointer `0x3fe0` and `0x4fe0` shown for `init` and `sh` are distinct from kernel stacks. These reside in user virtual address space, mapped by the process-specific page table in `p->pagetable`. The value `0x4fe0` = PGSIZE - 0x20, reflecting the stack's location at the top of the first user page with space reserved for initial arguments.[6][1]

### Design Tradeoffs

xv6's choice to pre-allocate all kernel stacks at boot trades memory efficiency for simplicity and predictability. A lazier approach (allocate on first fork) would complicate `allocproc` error handling and introduce allocation failure modes during process creation. Production kernels like Linux use per-CPU kernel stacks for interrupt handlers plus per-thread kernel stacks for process contexts, reducing memory footprint at the cost of increased complexity in interrupt entry paths.[6][9][1]

The fixed virtual address layout also prevents ASLR-style randomization of kernel stack positions, a security limitation. Modern kernels randomize stack positions to hinder ROP attacks that depend on knowing stack addresses.[1]

[1](https://pdos.csail.mit.edu/6.1810/2024/xv6/book-riscv-rev4.pdf)
[2](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)
[3](https://mes0903.github.io/OS/xv6-riscv-book-zh-TW/chapter3/)
[4](https://stackoverflow.com/questions/76192161/why-does-maxva-in-xv6-source-code-differ-from-the-value-in-the-xv6-textbook)
[5](https://stackoverflow.com/questions/79496492/raise-store-amo-page-fault-when-trying-write-stack-in-xv6s-trampoline)
[6](https://pekopeko11.sakura.ne.jp/unix_v6/xv6-book/en/The_first_process.html)
[7](https://utopianfuture.github.io/kernel/xv6.html)
[8](https://karthikv1392.github.io/cs3301_osn/slides/Tutorials/Xv6_notes_on_scheduling_and_trap_handling.pdf)
[9](https://pubs.thesciencein.org/journal/index.php/jist/article/download/a1053/678)
[10](https://dl.acm.org/doi/10.1145/3637494.3637495)
[11](https://ieeexplore.ieee.org/document/9643455/)
[12](https://www.semanticscholar.org/paper/1bdc3bdccc60b2c5ff18fa6b99ded72fa91235e0)
[13](https://www.semanticscholar.org/paper/93e1a7f8f4a86f7aa8a787711384005bdc94a21f)
[14](https://ieeexplore.ieee.org/document/11103326/)
[15](https://www.semanticscholar.org/paper/9e1ae3eed931d798ddc247313fc7f514add1b76d)
[16](https://www.semanticscholar.org/paper/4cf05d647bdfe1d72bba106e7c2b137422055629)
[17](https://www.semanticscholar.org/paper/88aae2fd65d3d0991804f142567256f46274c336)
[18](https://arxiv.org/html/2501.10189v1)
[19](http://arxiv.org/pdf/2502.04063.pdf)
[20](http://arxiv.org/pdf/2407.11999.pdf)
[21](http://arxiv.org/pdf/2504.06134.pdf)
[22](https://arxiv.org/pdf/2305.09266.pdf)
[23](http://arxiv.org/pdf/2502.20197.pdf)
[24](https://arxiv.org/pdf/2310.18353.pdf)
[25](https://arxiv.org/pdf/2302.03732.pdf)
[26](https://arxiv.org/pdf/2304.06167.pdf)
[27](https://arxiv.org/pdf/2401.16872.pdf)
[28](http://arxiv.org/pdf/2503.20590.pdf)
[29](http://arxiv.org/pdf/2502.10194.pdf)
[30](http://arxiv.org/pdf/2410.08396.pdf)
[31](http://arxiv.org/pdf/2411.14954.pdf)
[32](http://arxiv.org/pdf/2309.16509v1.pdf)
[33](https://arxiv.org/html/2407.12008v1)
[34](https://rcpassos.me/post/compiling-debugging-riscv-xv6-kernel)
[35](https://www.scribd.com/document/859877498/xv6-riscv)
[36](https://gitlab-research.centralesupelec.fr/damien.armillon/xv6-riscv-tp/-/blob/3e40d1639a0ae8a09428b9e240eeea340ec85ac9/kernel/proc.h)
[37](https://pdos.csail.mit.edu/6.828/2023/xv6/book-riscv-rev3.pdf)
[38](https://stackoverflow.com/questions/29448195/xv6-ptable-initialization)
[39](https://gitlab.com/x653/xv6-riscv-fpga)
[40](https://xiayingp.gitbook.io/build_a_os/virtual-memory/xv6-virtual-memory)
[41](https://pdos.csail.mit.edu/6.828/2025/quiz/q24-1-sol.pdf)
[42](https://cs326-s25.cs.usfca.edu/guides/page-tables)
[43](https://www.cse.iitb.ac.in/~mythili/os/notes/old-xv6/xv6-process.pdf)
[44](https://stackoverflow.com/questions/67836212/i-cant-understand-this-line-of-code-in-xv6)
[45](https://www.cs.williams.edu/~cs432/lectures/Lecture05.pdf)
[46](https://sameerismail.org/xv6-usermode)
[47](https://www.youtube.com/watch?v=-S0Z0CmwkEk)
[48](https://arxiv.org/pdf/2311.10283.pdf)
[49](https://www.mdpi.com/2410-387X/7/4/58/pdf?version=1700127856)
[50](http://arxiv.org/pdf/2111.01949.pdf)
[51](https://arxiv.org/html/2411.07721v1)
[52](https://arxiv.org/html/2407.13326v1)
[53](http://arxiv.org/pdf/2404.01861.pdf)
[54](https://arxiv.org/pdf/2302.02969.pdf)
[55](https://arxiv.org/pdf/2104.00762.pdf)
[56](http://arxiv.org/pdf/2412.08769.pdf)
[57](https://arxiv.org/pdf/2411.09543.pdf)
[58](https://arxiv.org/html/2504.05718v1)
[59](https://arxiv.org/pdf/2212.05614.pdf)
[60](https://arxiv.org/pdf/2106.09966.pdf)
[61](https://arxiv.org/pdf/2401.17984.pdf)
[62](https://git.baguette.netlib.re/Bricoles/xv6-riscv/src/commit/4fdf4028a4b61199abd781408842fa9ac8044c53/notes/chapter3-page-tables)
[63](https://pdos.csail.mit.edu/6.828/2025/xv6/xv6-src-booklet-rev5.pdf)
[64](https://miaochenlu.github.io/2020/12/19/xv6-lab3/)
[65](https://git.baguette.netlib.re/Bricoles/xv6-riscv/commit/4fdf4028a4b61199abd781408842fa9ac8044c53)
[66](https://www.cs.columbia.edu/~junfeng/11sp-w4118/lectures/mem.pdf)
[67](https://www.youtube.com/watch?v=_p_MvMnwJbE)
[68](https://gitlab.eduxiji.net/educg-group-17064-1466468/202310336101415-3102/-/blob/main/kernel/vm.c)
[69](https://www.cse.iitd.ac.in/~sbansal/os/previous_years/2011/xv6_html/proc_8c.html)
[70](https://intra.ece.ucr.edu/~cong/teaching/UCR/AOS/labs/lab3.pdf)
[71](https://gitlab-student.centralesupelec.fr/aymeric.chaumont/xv6-riscv-tp/-/blob/3e49190884767db8b9f24c348e8ff623a5b20171/kernel/proc.c)
[72](https://pdos.csail.mit.edu/6.828/2012/xv6/book-rev7.pdf)
[73](https://xiayingp.gitbook.io/build_a_os/traps-and-interrupts/untitled-3)
[74](http://csl.snu.ac.kr/courses/4190.307/2023-2/11-vmimpl.pdf)
[75](http://arxiv.org/pdf/2312.08117.pdf)
[76](http://arxiv.org/pdf/1809.00626.pdf)
[77](https://arxiv.org/pdf/2502.18832.pdf)
[78](http://arxiv.org/pdf/2401.17618.pdf)
[79](http://arxiv.org/pdf/2503.16950.pdf)
[80](http://arxiv.org/pdf/2406.05594.pdf)
[81](http://arxiv.org/pdf/2307.14471.pdf)
[82](http://arxiv.org/pdf/2202.05732.pdf)
[83](http://arxiv.org/pdf/2403.04635.pdf)
[84](http://ispras.ru/proceedings/docs/2018/30/3/isp_30_2018_3_121.pdf)
[85](http://arxiv.org/pdf/2312.13462.pdf)
[86](https://arxiv.org/pdf/2411.18094.pdf)
[87](http://arxiv.org/pdf/2406.17796.pdf)
[88](https://arxiv.org/pdf/1605.05810.pdf)
[89](https://arxiv.org/pdf/2406.07278.pdf)
[90](https://www.cs.usfca.edu/~mmalensek/cs326/schedule/materials/xv6-book.pdf)
[91](https://stackoverflow.com/questions/47146966/how-to-modify-the-user-space-and-kernel-space-in-xv6)
[92](https://www.youtube.com/watch?v=NWicJxVENjg)
[93](https://www.studocu.com/en-nz/document/auckland-university-of-technology/operating-systems/xv6-book/74387940)
[94](https://pdos.csail.mit.edu/6.828/2018/xv6/xv6-rev10.pdf)
[95](https://xiayingp.gitbook.io/build_a_os/os-organization/untitled)
[96](https://www.youtube.com/watch?v=BB0Tbyu3XMQ)
[97](https://ithelp.ithome.com.tw/articles/10298862)
[98](https://docs.kernel.org/6.6/riscv/vm-layout.html)
[99](https://www.youtube.com/watch?v=NC_qkXznvkg)
[100](https://stackoverflow.com/questions/tagged/virtual-address-space)
[101](https://www.rose-hulman.edu/class/csse/csse332/2324b/notes/project_hints/)


The user stack pointer 0x3fe0 and 0x4fe0 shown for init and sh are distinct from kernel stacks. These reside in user virtual address space

 i wanna verify. i was thinking maybe i could find the files related to new process creation , so i could add logs to specific places after each component for a new process is implemented so i can see the full address space and its contents


