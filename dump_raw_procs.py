import gdb

class DumpRawProcs(gdb.Command):
    def __init__(self):
        super(DumpRawProcs, self).__init__("dump-raw-procs", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        try:
            # Get process table base address
            proc = gdb.parse_and_eval('proc')
            print(f"\nProcess Table Base @ {proc}")
            
            # Print each process entry
            for i in range(64):  # NPROC is typically 64 in xv6
                print(f"\n=== Process {i} ===")
                gdb.execute(f"x/32x &proc[{i}]")
        except Exception as e:
            print(f"Error: {e}")

DumpRawProcs()
            # Get process table base address from kernel symbol
            proc_array = gdb.parse_and_eval("proc")
            proc_array_addr = int(str(proc_array).split()[0], 16)
            
            print(f"\nProcess Table Base Address: 0x{proc_array_addr:x}")
            
            # Get NPROC value from kernel
            nproc = int(gdb.parse_and_eval("NPROC"))
            print(f"NPROC: {nproc}")
            
            # Get size of proc struct
            proc_size = gdb.parse_and_eval("sizeof(struct proc)")
            print(f"Size of struct proc: {proc_size} bytes\n")
            
            # Dump raw contents of each proc entry
            for i in range(nproc):
                p = proc_array[i]
                p_addr = int(str(p).split()[0], 16)
                
                print(f"\nProc[{i}] @ 0x{p_addr:x}:")
                print(f"state: {int(p['state'])}")
                print(f"pid: {int(p['pid'])}")
                print(f"parent: 0x{int(p['parent']):x}")
                print(f"kernel stack: 0x{int(p['kstack']):x}")
                print(f"size: {int(p['sz'])}")
                print(f"page table: 0x{int(p['pagetable']):x}")
                print(f"trapframe: 0x{int(p['trapframe']):x}")
                print(f"context ra: 0x{int(p['context']['ra']):x}")
                print(f"context sp: 0x{int(p['context']['sp']):x}")
                print(f"chan: 0x{int(p['chan']):x}")
                print(f"killed: {int(p['killed'])}")
                print(f"xstate: {int(p['xstate'])}")
                print(f"name: {p['name'].string()}")

# Register command
DumpRawProcsCommand()
print("Raw process table dump command loaded. Use 'dump-raw-procs' to see raw process table contents.")