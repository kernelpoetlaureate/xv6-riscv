Annotate memdump output with kernel symbols
==========================================

This small tool helps you map a raw `memdump` output (from the userland
`memdump` program) to kernel symbols found in `kernel/kernel.sym`.

Files
- `tools/annotate_memdump.py` - reads memdump text from stdin and prints the
  same rows annotated with owning kernel symbol(s).

Quick usage

1. Produce a memdump inside xv6 (example):

   memdump 0x8000168a 64 > dump.txt

   (If you run `memdump` against a kernel address it uses the `kread` syscall
   to fetch kernel memory.)

2. Annotate it locally (on your host) with the kernel symbol table:

   cat dump.txt | python3 tools/annotate_memdump.py --sym kernel/kernel.sym

Example (what you might see):

  Memory at 0x000000008000168A:
  168A: EF F0 9F DB ...
    start -> vm.c+0x68A | end -> vm.c+0x69D | owner: vm.c (0x0-0xF)

Notes and next steps
- The script relies on `kernel/kernel.sym`. Rebuild your kernel if symbols
  moved or you rebuilt the kernel.
- The script is an offline annotator. To map arbitrary addresses to their
  owners in real time inside xv6 you'd need a kernel helper (syscall) that
  returns the owner information for an address. That syscall could:
  - check whether the address is in kernel space and find the symbol (as we
    do here), or
  - walk the current process' page table (or scan all procs) to see which
    process maps this virtual address, and return pid + virtual mapping info,
  - for physical page owners, check the kernel allocator structures (e.g.
    page reference counts / kmem lists) to attribute ownership.

If you'd like, I can: implement a kernel syscall `mapinfo(addr)` that returns
structured ownership info, or extend the annotator to also consult a dump of
kernel data structures (proc table, page tables) to attribute pages to
processes.
parse_qemu_log
=================

Small helper to parse flat qemu/kernel logs (like `kernel/data.md`) and emit
neater outputs (Markdown summary, CSV of freed pages).

Usage
-----

Run from the repository root (WSL on Windows):

python3 tools/parse_qemu_log.py kernel/data.md tools/output

This writes `tools/output.md` and `tools/output-kfree.csv`.

What it extracts
----------------

- kfree freed page addresses
- a compact proc table summary (index, addr, pid, state, kstack)
- syscall lines (PID, syscall number, name, heap end, user SP) and RETURNs

Notes & next steps
------------------

- The script is intentionally small and tolerant; it doesn't attempt to parse
  the full verbose dumps or raw hexdumps into structured binary fields.
- If you want HTML output, JSON, or richer tables (e.g., searching/interactive),
  I can extend the script to emit them.

Capture + run
-------------

There's also a helper to run a command and capture its console output. Use:

python3 tools/capture_and_process.py -- <your command>

Example (simulate capture):

python3 tools/capture_and_process.py -- cat kernel/data.md

To build and run xv6 (your usual flow) and capture its output automatically:

python3 tools/capture_and_process.py --run-xv6

This runs `make clean && make qemu` under a shell (so `&&` chaining works),
records the raw console output to `tools/raw/<timestamp>.log`, and produces
`tools/output-<timestamp>.md` and `tools/output-<timestamp>-kfree.csv`.
