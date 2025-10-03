set confirm off
set architecture riscv:rv64
target remote 127.0.0.1:26000
symbol-file kernel/kernel
set disassemble-next-line auto
set riscv use-compressed-breakpoints yes

# Load Python extensions for memory analysis
source dump_procs.py
source dump_kernel.py
source dump_pagetable.py
source dump_memory_allocator.py
source dump_cpu_detail.py

# Set initial breakpoint at syscall
break syscall

# Print startup message
echo \nXV6 Debug Session Ready!\nType 'c' or 'continue' to start execution.\n
