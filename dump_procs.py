import gdb

class DumpProcsCommand(gdb.Command):
    """Dump all active xv6 processes from the process table."""
    
    def __init__(self):
        super(DumpProcsCommand, self).__init__("dump_procs", gdb.COMMAND_DATA)
    
    def invoke(self, arg, from_tty):
        # Get process table array
        proc_array = gdb.parse_and_eval("proc")
        
        # Determine array size (NPROC)
        nproc = 64  # Default for xv6, adjust if you've modified NPROC
        
        # State enum mapping (from kernel/proc.h)
        state_names = ["UNUSED", "USED", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE"]
        
        print("=" * 60)
        print("XV6 PROCESS TABLE DUMP")
        print("=" * 60)
        
        for i in range(nproc):
            p = proc_array[i]
            state = int(p['state'])
            
            # Skip unused entries
            if state == 0:
                continue
            
            print(f"\n----- PROCESS TABLE ENTRY {i} -----")
            print(f"PID: {int(p['pid'])}")
            print(f"Name: {p['name'].string()}")
            print(f"State: {state_names[state] if state < len(state_names) else 'UNKNOWN'}")
            
            # Safely dereference parent pointer
            parent_ptr = int(p['parent'])
            if parent_ptr != 0:
                try:
                    parent = p['parent'].dereference()
                    print(f"Parent PID: {int(parent['pid'])}")
                except:
                    print(f"Parent: 0x{parent_ptr:x} (cannot dereference)")
            else:
                print(f"Parent PID: None")
            
            print(f"Killed: {int(p['killed'])}")
            print(f"Exit status: {int(p['xstate'])}")
            print(f"Memory Size: {int(p['sz'])} bytes")
            print(f"Kernel Stack: 0x{int(p['kstack']):x}")
            print(f"Page table: 0x{int(p['pagetable']):x}")
            print(f"Trapframe: 0x{int(p['trapframe']):x}")
            print(f"Context SP: 0x{int(p['context']['sp']):x}")
            print(f"Context RA: 0x{int(p['context']['ra']):x}")
            print(f"CWD: 0x{int(p['cwd']):x}")
            print(f"Chan: 0x{int(p['chan']):x}")
            
            print("Open files:")
            for j in range(16):
                ofile = int(p['ofile'][j])
                if ofile != 0:
                    print(f"  [{j}] 0x{ofile:x}")
            
            print(f"----- END PROCESS {i} -----")

# Register the command
DumpProcsCommand()

