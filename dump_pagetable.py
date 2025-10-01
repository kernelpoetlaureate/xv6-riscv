import gdb

class DumpPageTableCommand(gdb.Command):
    """Recursively dump xv6 RISC-V page table hierarchy."""
    
    def __init__(self):
        super(DumpPageTableCommand, self).__init__("dump_pagetable", gdb.COMMAND_DATA)
    
    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)
        if len(args) < 1:
            print("Usage: dump-pagetable <pid>")
            return
        
        pid = int(args[0])
        
        # Find process in process table
        proc_array = gdb.parse_and_eval("proc")
        target_proc = None
        
        for i in range(64):
            if int(proc_array[i]['state']) != 0 and int(proc_array[i]['pid']) == pid:
                target_proc = proc_array[i]
                break
        
        if target_proc is None:
            print(f"Process with PID {pid} not found")
            return
        
        pagetable_pa = int(target_proc['pagetable'])
        if pagetable_pa == 0:
            print(f"Process PID {pid} has no page table (pagetable = 0x0)")
            return
        
        print(f"Page table for PID {pid} ({target_proc['name'].string()})")
        print(f"Root page table physical address: 0x{pagetable_pa:x}")
        print("=" * 80)
        
        self.walk_pagetable(pagetable_pa, 2, 0)
    
    def walk_pagetable(self, pa, level, va_prefix):
        """Recursively walk page table levels."""
        
        # Convert physical address to kernel virtual address for access
        # RISC-V xv6: PA + KERNBASE (0x80000000)
        KERNBASE = 0x80000000
        kva = pa + KERNBASE
        
        try:
            # Cast to pointer to uint64 array (512 entries per page table)
            pte_array = gdb.Value(kva).cast(gdb.lookup_type("uint64").pointer())
        except:
            print(f"Error: Cannot access page table at PA 0x{pa:x} (KVA 0x{kva:x})")
            return
        
        for i in range(512):
            try:
                pte = int(pte_array[i])
            except:
                continue
            
            if (pte & 0x1) == 0:  # Not valid
                continue
            
            # Extract physical page number and flags
            ppn = (pte >> 10) & 0xFFFFFFFFFFF
            child_pa = ppn << 12
            flags = pte & 0x3FF
            
            # Decode flags
            flag_str = ""
            flag_str += "V" if flags & 0x1 else "-"
            flag_str += "R" if flags & 0x2 else "-"
            flag_str += "W" if flags & 0x4 else "-"
            flag_str += "X" if flags & 0x8 else "-"
            flag_str += "U" if flags & 0x10 else "-"
            flag_str += "G" if flags & 0x20 else "-"
            flag_str += "A" if flags & 0x40 else "-"
            flag_str += "D" if flags & 0x80 else "-"
            
            # Compute virtual address for this entry
            va_bits = (va_prefix << 9) | i
            if level == 2:
                va = va_bits << 30
            elif level == 1:
                va = va_bits << 21
            else:  # level == 0
                va = va_bits << 12
            
            indent = "  " * (2 - level)
            
            # Check if inner node or leaf
            is_leaf = (flags & 0xE) != 0  # R, W, or X set
            
            if is_leaf:
                print(f"{indent}L{level}[{i:3d}]: VA 0x{va:016x} -> PA 0x{child_pa:016x} [{flag_str}]")
            else:
                print(f"{indent}L{level}[{i:3d}]: Inner node -> PA 0x{child_pa:016x} [{flag_str}]")
                if level > 0:
                    self.walk_pagetable(child_pa, level - 1, va_bits)

# Register the command
DumpPageTableCommand()

