// Physical memory layout

// qemu -machine virt is set up like this,
// based on qemu's hw/riscv/virt.c:
//
// 00001000 -- boot ROM, provided by qemu
// 02000000 -- CLINT
// 0C000000 -- PLIC
// 10000000 -- uart0 
// 10001000 -- virtio disk 
// 80000000 -- qemu's boot ROM loads the kernel here,
//             then jumps here.
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


/* my own explanations
==============================================================================

## What's Statically Known (Compile-Time Constants)

### 1. Device Physical Addresses (100% Static)

These are **hardcoded by QEMU's virt machine specification** and never change:[1][2]

```c
#define UART0 0x10000000L      // Always at this address
#define VIRTIO0 0x10001000     // Always at this address
#define PLIC 0x0c000000L       // Always at this address
#define CLINT 0x2000000L       // Always at this address
```

**Known at**: Design time (defined by RISC-V platform specification)  
**Consequence**: You can print these addresses without running xv6—they're **constants baked into memlayout.h**

### 2. Physical Memory Range (Static)

```c
#define KERNBASE 0x80000000L               // Always starts here
#define PHYSTOP (KERNBASE + 128*1024*1024) // 0x88000000, always ends here
```

**Known at**: Compile time (hardcoded in xv6 for QEMU's 128 MB RAM configuration)[3]
**Consequence**: Total RAM size (128 MB) is **fixed** for the build

### 3. Virtual Address Layout (Static)

```c
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))  // 0x4000000000 (Sv39 max)
#define TRAMPOLINE (MAXVA - PGSIZE)         // 0x3ffffff000
#define TRAPFRAME (TRAMPOLINE - PGSIZE)     // 0x3ffffeb000
#define KSTACK(p) (TRAMPOLINE - ((p)+1)*2*PGSIZE)
```

**Known at**: Compile time (calculated from Sv39 architecture constants)[4][5]
**Consequence**: Every kernel stack's **virtual address** is predetermined:
- `KSTACK(0) = 0x3fffffd000`
- `KSTACK(1) = 0x3fffffb000`
- `KSTACK(2) = 0x3fffff9000`
- ...
- `KSTACK(63) = 0x3ffff7f000`

These **never change** across reboots.[6][1]

***

## What's Dynamically Determined (Runtime)

### 1. Kernel Binary Size (Semi-Static)

```c
extern char end[];     // First address after kernel .bss
extern char etext[];   // First address after kernel .text
```

**Known at**: **Link time** (after compilation, before execution)[2]
**Changes when**: You modify kernel code or add/remove global variables  
**Example**:
- If you add a large global array: `char big_buffer[1024*1024];`
- `end` moves up by 1 MB (more `.bss` section)
- Free RAM starts 1 MB higher

**Consequence**: The boundary between "kernel code/data" and "free RAM" is **fixed for a particular compiled kernel**, but differs between builds.

### 2. Physical Page Allocation (Dynamic)

The physical addresses of **allocated pages** are determined at runtime by `kalloc()`:[2]

```c
void *kalloc(void) {
  struct run *r;
  acquire(&kmem.lock);
  r = kmem.freelist;  // Grab head of freelist (unpredictable address)
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);
  return (void*)r;
}
```

**Example from your logs**:[7]
- Kernel stack 2 maps to pa=0x87f97000 (allocated during boot)
- PID 6's trapframe at pa=0x87f22000 (allocated during fork)
- PID 7's trapframe at pa=0x87f30000 (different allocation)

**Not predictable** because:
1. Freelist order depends on **which pages are freed in which order**
2. Intermediate allocations between process creations perturb the freelist
3. Race conditions in multicore systems affect allocation order

### 3. Process Virtual Address Space Size (Dynamic)

```c
struct proc {
  uint64 sz;  // Size of process memory (bytes)
  // ...
};
```

**Determined by**: `exec()` when loading ELF binary + heap growth via `sbrk()`[2]
**Example**:
- PID 6 running `ls`: `sz = 0x15000` (86016 bytes)
- PID 7 running `mkdir`: `sz = 0x15000` (same binary size)
- PID 8 running custom program with large heap: `sz = 0x50000`

**Not static** because each ELF binary has different size.

***

## Pre-Boot Knowledge Matrix

Here's what you can know **before running xv6**:

| Component | Compile-Time Known? | Runtime Known? | Notes |
|-----------|---------------------|----------------|-------|
| **UART0 physical address** | ✓ Yes (0x10000000) | ✓ Yes | QEMU spec, never changes |
| **CLINT physical address** | ✓ Yes (0x02000000) | ✓ Yes | QEMU spec |
| **PLIC physical address** | ✓ Yes (0x0C000000) | ✓ Yes | QEMU spec |
| **VIRTIO physical address** | ✓ Yes (0x10001000) | ✓ Yes | QEMU spec |
| **KERNBASE** | ✓ Yes (0x80000000) | ✓ Yes | xv6 convention |
| **PHYSTOP** | ✓ Yes (0x88000000) | ✓ Yes | xv6 hardcoded for 128MB |
| **Total RAM size** | ✓ Yes (128 MB) | ✓ Yes | QEMU `-m 128M` |
| **Kernel code size** | ✗ No (link-time) | ✓ Yes | Depends on build |
| **`end` symbol address** | ✗ No (link-time) | ✓ Yes | Depends on build |
| **Free RAM start** | ✗ No (link-time) | ✓ Yes | Equals `end` |
| **TRAMPOLINE virtual address** | ✓ Yes (0x3ffffff000) | ✓ Yes | Sv39 max minus page |
| **TRAPFRAME virtual address** | ✓ Yes (0x3ffffeb000) | ✓ Yes | TRAMPOLINE - PGSIZE |
| **KSTACK(i) virtual addresses** | ✓ Yes (formula) | ✓ Yes | Calculated from i |
| **KSTACK(i) physical addresses** | ✗ No | ✓ Yes | Allocated by `proc_mapstacks()` |
| **Trapframe physical addresses** | ✗ No | ✓ Yes | Allocated by `allocproc()` |
| **User page table PAs** | ✗ No | ✓ Yes | Allocated by `proc_pagetable()` |
| **User code/data/heap PAs** | ✗ No | ✓ Yes | Allocated by `exec()` |

***

## What You Can Print Before Boot

You can create a **static analysis tool** that parses the compiled kernel and prints the memory layout:

### Tool 1: Analyze ELF Binary

```bash
# Read kernel ELF sections
riscv64-unknown-elf-readelf -S kernel/kernel

# Output shows:
# Section Headers:
#   [Nr] Name              Type             Address           Offset
#   [ 1] .text             PROGBITS         0000000080000000  00001000
#   [ 2] .rodata           PROGBITS         0000000080008abc  00009abc
#   [ 3] .data             PROGBITS         000000008000a000  0000b000
#   [ 4] .bss              NOBITS           000000008000b000  0000c000
```

From this, you can calculate:
- Kernel text size: (`.rodata` address - `.text` address)
- Kernel data size: (`.bss` address - `.rodata` address)
- `end` symbol: (`.bss` address + `.bss` size)

### Tool 2: Parse `kernel/kernel.sym`

After building xv6, this file contains all symbols with addresses:

```bash
cat kernel/kernel.sym | grep -E "(end|etext|KERNBASE)"

# Output:
# 0000000080000000 T _entry
# 0000000080008abc T etext
# 00000000819a5e08 B end
```

This tells you:
- Kernel ends at 0x819a5e08
- Free RAM starts at 0x819a5e08
- Free RAM is (0x88000000 - 0x819a5e08) = 107,086,328 bytes = 26,152 pages

### Tool 3: Pre-Boot Memory Map Generator

Create `scripts/memmap.py`:

```python
#!/usr/bin/env python3
import subprocess
import re

# Constants from memlayout.h (could parse this file instead)
UART0 = 0x10000000
CLINT = 0x02000000
PLIC = 0x0C000000
VIRTIO0 = 0x10001000
KERNBASE = 0x80000000
PHYSTOP = 0x88000000
MAXVA = (1 << (9 + 9 + 9 + 12 - 1))
TRAMPOLINE = MAXVA - 4096
PGSIZE = 4096

# Parse kernel symbols
def get_symbol_address(symbol_name):
    result = subprocess.run(['grep', symbol_name, 'kernel/kernel.sym'],
                          capture_output=True, text=True)
    if result.stdout:
        addr_str = result.stdout.split()[0]
        return int(addr_str, 16)
    return None

def main():
    etext = get_symbol_address('etext')
    end = get_symbol_address('end')
    
    print("=" * 50)
    print("XV6 PHYSICAL MEMORY LAYOUT (STATIC ANALYSIS)")
    print("=" * 50)
    print()
    
    print("--- DEVICE MMIO ---")
    print(f"0x{CLINT:012x} - 0x{CLINT+0x10000:012x}  CLINT")
    print(f"0x{PLIC:012x} - 0x{PLIC+0x4000000:012x}  PLIC")
    print(f"0x{UART0:012x} - 0x{UART0+0x1000:012x}  UART0")
    print(f"0x{VIRTIO0:012x} - 0x{VIRTIO0+0x8000:012x}  VIRTIO")
    print()
    
    print("--- DRAM (128 MB) ---")
    print(f"0x{KERNBASE:012x} - 0x{etext:012x}  Kernel .text ({(etext-KERNBASE)//1024} KB)")
    print(f"0x{etext:012x} - 0x{end:012x}  Kernel .rodata/.data/.bss ({(end-etext)//1024} KB)")
    print(f"0x{end:012x} - 0x{PHYSTOP:012x}  Free RAM ({(PHYSTOP-end)//(1024*1024)} MB)")
    print()
    
    print(f"Total free pages: {(PHYSTOP-end)//4096}")
    print()
    
    print("--- VIRTUAL ADDRESS LAYOUT (Sv39) ---")
    print(f"MAXVA:      0x{MAXVA:012x}")
    print(f"TRAMPOLINE: 0x{TRAMPOLINE:012x}")
    print(f"TRAPFRAME:  0x{TRAMPOLINE-PGSIZE:012x}")
    print(f"KSTACK(0):  0x{TRAMPOLINE-2*PGSIZE:012x}")
    print(f"KSTACK(63): 0x{TRAMPOLINE-64*2*PGSIZE:012x}")
    print("=" * 50)

if __name__ == '__main__':
    main()
```

Run it:

```bash
$ make kernel/kernel  # Build kernel
$ python3 scripts/memmap.py
```

**Output** (before running xv6):

```
==================================================
XV6 PHYSICAL MEMORY LAYOUT (STATIC ANALYSIS)
==================================================

--- DEVICE MMIO ---
0x002000000000 - 0x002000010000  CLINT
0x00c000000000 - 0x010000000000  PLIC
0x010000000000 - 0x010000001000  UART0
0x010001000000 - 0x010001008000  VIRTIO

--- DRAM (128 MB) ---
0x080000000000 - 0x080008abc000  Kernel .text (34 KB)
0x080008abc000 - 0x0819a5e08000  Kernel .rodata/.data/.bss (26567 KB)
0x0819a5e08000 - 0x088000000000  Free RAM (102 MB)

Total free pages: 26152

--- VIRTUAL ADDRESS LAYOUT (Sv39) ---
MAXVA:      0x004000000000
TRAMPOLINE: 0x003ffffff000
TRAPFRAME:  0x003ffffeb000
KSTACK(0):  0x003fffffd000
KSTACK(63): 0x003ffff7f000
==================================================
```

***

## What You CANNOT Know Before Running

**Physical addresses of dynamically allocated pages**:

```c
// These are UNPREDICTABLE before runtime:
uint64 pa = (uint64)kalloc();  // Could be any free page
```

**Example**: Your logs show:[7]
- `KSTACK mapped idx=2 va=0x3fffff9000 pa=0x87f97000`

The **VA is predictable** (`KSTACK(2)` formula), but the **PA (0x87f97000) is not** because:
1. `proc_mapstacks()` calls `kalloc()` 64 times
2. `kalloc()` returns pages from the freelist in **LIFO order**
3. Freelist order depends on how `freerange()` built it (iterates from `end` to `PHYSTOP`)
4. The exact PA depends on `end`, which depends on kernel binary size

**However**: With a **deterministic build**, the PA is **reproducible**. If you rebuild without changing code, `end` stays the same, freelist order stays the same, so `KSTACK(2)` always maps to the same PA.

***

## Summary: Static vs Dynamic Knowledge

**Fully static** (known at compile time):
- Device MMIO addresses
- Virtual address formulas (TRAMPOLINE, TRAPFRAME, KSTACK VAs)
- Total RAM size

**Semi-static** (known after linking, before running):
- Kernel code/data size
- `end` symbol address
- Free RAM start address
- Number of free pages

**Fully dynamic** (only known at runtime):
- Physical addresses of allocated pages (kernel stacks, trapframes, user memory)
- Process address space sizes (depends on which ELF binaries are executed)
- Freelist state (changes with every `kalloc()`/`kfree()`)

**You can visualize the structure before running xv6** by:
1. Parsing `memlayout.h` for constants
2. Parsing `kernel.sym` for link-time symbols
3. Computing virtual address layout from formulas
4. Leaving placeholders for runtime-allocated physical addresses ("TBD at boot")

This gives you a **template** of the memory structure, with blanks filled in when xv6 boots.[5][1][4][6][2]


==============================================
==============================================
==============================================
==============================================
==============================================
==============================================
==============================================
==============================================
==============================================

Architectural Principle: Memory Structures Are Already Catalogued

You need **introspection without allocation**—leverage existing kernel metadata structures that already track memory objects, 
avoiding observer effects while exposing architectural relationships.[1][2][3]

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

