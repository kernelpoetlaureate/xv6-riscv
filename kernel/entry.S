        # Phase 1: entry.S — Stack Setup (0x80000000)
        # Execution lands at _entry with paging DISABLED (va == pa)
        # qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
_entry:
        # Each hart allocates per-CPU stack from stack0 array:
        # sp = stack0 + (hartid * 4096) - enables C function calls
        # set up a stack for C.
        # stack0 is declared in start.c,
        # with a 4096-byte stack per CPU.
        # sp = stack0 + ((hartid + 1) * 4096)
        la sp, stack0
        li a0, 1024*4
        csrr a1, mhartid
        addi a1, a1, 1
        mul a0, a0, a1
        add sp, sp, a0
        # Phase 1→2 transition: call start() for machine-mode config
        # jump to start() in start.c
        call start
spin:
        j spin
