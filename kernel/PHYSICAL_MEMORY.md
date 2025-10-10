Physical memory layout and conventions in this xv6-riscv tree

This document collects where the kernel describes and implements physical memory characteristics, the important constants/macros, and how the allocator and page tables treat physical vs virtual addresses.

Files referenced
- `memlayout.h` — physical address constants (KERNBASE, PHYSTOP, MMIO addresses, TRAMPOLINE).
- `riscv.h` — page size, Sv39 constants, and `MAXVA`.
- `kalloc.c` — physical page allocator and `kinit()`/`freerange()` behaviour.
- `vm.c` — kernel page-table creation (`kvmmake`, `kvmmap`, `mappages`) where kernel VA↔PA mappings are established.
- `pageinfo.h` / `pageinfo.c` — debug table that indexes by physical-page number and records observed PAs and VAs.
- `kernel.ld` and `trampoline.S` — linker placement that ensures the kernel/trampoline are located at `KERNBASE` / TRAMPOLINE addresses.

Key constants and their meanings (exact values in this tree)
- PGSIZE = 4096 (riscv.h)
- MAXVA = 1 << 38 (riscv.h). User VAs must be < MAXVA.
- KERNBASE = 0x80000000 (memlayout.h). Kernel virtual base and where the kernel is loaded.
- PHYSTOP = KERNBASE + 128*1024*1024 = 0x88000000 (memlayout.h). End of usable physical memory for the kernel; `pageinfo` is sized to `PHYSTOP/PGSIZE`.
- TRAMPOLINE = MAXVA - PGSIZE (memlayout.h). Mapped into the highest user-space VA and the kernel.
- MMIO constants: UART0 = 0x10000000, VIRTIO0 = 0x10001000, PLIC = 0x0c000000 (memlayout.h).

Where physical memory characteristics are implemented

1) Linker script (`kernel/kernel.ld`)
   - The script sets the starting address for the kernel image: `. = 0x80000000;` — this makes `_entry` start at `KERNBASE` so the kernel's layout and symbol addresses are based on KERNBASE.
   - The trampoline page is placed in a dedicated `trampsec` section and an `_trampoline` symbol is set to its base; `trampoline.S` uses these symbols.

2) Kernel paging setup (`kernel/vm.c`)
   - `kvmmake()` creates the kernel page table and calls `kvmmap(kpgtbl, VA, PA, sz, perm)` multiple times with identical VA and PA for kernel region and MMIO regions. This produces a direct mapping for kernel virtual addresses in the range `[KERNBASE, PHYSTOP)` to the same numeric physical addresses.
   - `kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R|PTE_X)` for the kernel text.
   - `kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R|PTE_W)` maps the kernel data and the usable physical RAM.
   - MMIO regions (UART0, VIRTIO0, PLIC) are mapped similarly with VA == PA.

3) Physical allocator and initialization (`kernel/kalloc.c`)
   - `kinit()` calls `freerange(end, PHYSTOP)` which calls `kfree()` to add each physical page in that range to the allocator freelist.
   - `kalloc()` returns a pointer to a physical page (the pointer is in the kernel address range and, due to the direct mapping, can be used as a kernel virtual address).
   - Boot-time logging prints messages like: `kinit: freerange from 0x... to 0x...` — you can see these in runtime logs/dumps.

4) Pageinfo (`kernel/pageinfo.h` / `kernel/pageinfo.c`)
   - `pageinfo` is an array indexed by physical-page number: `PA2IDX(pa) = (pa) / PGSIZE`.
   - It records `type`, `owner_pid`, `mapped_va` (last observed VA), `tag` and `ref` (best-effort). The `pa` you see in dumps is derived from the index via `IDX2PA(idx) = idx * PGSIZE`.
   - `pageinfo` is sized to `PHYSTOP/PGSIZE` (NPAGE), so it only covers physical pages from 0..PHYSTOP-1.

Behavioral notes and semantics
- Kernel direct mapping: Because `kvmmake()` maps kernel VAs ≥ `KERNBASE` to PAs with the same numeric value, kernel pointers returned by `kalloc()` have the same numeric value as the physical frame they back. This is why `pageinfo` records the allocator-supplied pointer as `pa`.
- `pageinfo` is best-effort and not authoritative: it can miss early allocations (before initialization), and `ref`/`owner_pid` are heuristics. Use `kalloc`/`kfree` and the allocator freelist for authoritative allocator state.
- MMIO: some physical addresses are not normal RAM (UART0, PLIC, VIRTIO0); the kernel still maps these physical addresses into the kernel page table so code can access device registers via virtual addresses that numerically match the device physical addresses.
- PHYSTOP limits `pageinfo` and the allocator's usable RAM. Physical addresses >= `PHYSTOP` are not tracked by `pageinfo` and are outside the kernel's declared usable RAM.

Examples
- If `kalloc()` returns `0x80005000`, that numeric value is used as both the kernel VA and the recorded PA (because of the 1:1 kernel mapping). `pageinfo` will index `0x80005000 / 4096` to find the corresponding `pageinfo` entry and print `pa=0x80005000` and `va=0x80005000` (if the mapping was observed).
- `dump_pages.txt` logs show runtime messages such as `kinit: freerange from 0x0000000080024c78 to 0x0000000088000000`, confirming the allocator initialized pages in the physical range `[end, PHYSTOP)`.

Where to extend or clarify
- If you want a guarantee that `pa` in `pageinfo` is always the physical address (even in non-1:1 mappings), `kalloc()` could pass a translated PA explicitly (e.g., via a `physical_of(void *kva)` helper). Currently the code relies on the kernel 1:1 mapping.
- Consider adding a small documented helper / syscall to print VA→PA translations using `walkaddr_any()` for debugging.

References (key places in the tree)
- `kernel/memlayout.h`
- `kernel/riscv.h`
- `kernel/kalloc.c`
- `kernel/vm.c`
- `kernel/pageinfo.h`, `kernel/pageinfo.c`
- `kernel/kernel.ld`, `kernel/trampoline.S`