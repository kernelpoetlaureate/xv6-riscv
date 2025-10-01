import gdb

class DumpKernelCommand(gdb.Command):
    """Dump all major xv6 kernel data structures."""
    
    def __init__(self):
        super(DumpKernelCommand, self).__init__("dump_kernel", gdb.COMMAND_DATA)
    
    def invoke(self, arg, from_tty):
        print("=" * 80)
        print("XV6 KERNEL DATA STRUCTURE COMPLETE DUMP")
        print("=" * 80)
        
        self.dump_physical_memory()
        self.dump_cpus()
        self.dump_processes()
        self.dump_kmem()
        self.dump_bcache()
        self.dump_icache()
        self.dump_ftable()
        self.dump_devsw()
        self.dump_log()
    
    def dump_physical_memory(self):
        """Dump physical memory layout information."""
        print("\n" + "=" * 80)
        print("PHYSICAL MEMORY LAYOUT")
        print("=" * 80)
        
        try:
            # Get memory layout constants from kernel
            kernbase = int(gdb.parse_and_eval("KERNBASE"))
            phystop = int(gdb.parse_and_eval("PHYSTOP"))
            
            print(f"KERNBASE (kernel virtual base):  0x{kernbase:016x}")
            print(f"PHYSTOP  (end of physical RAM):  0x{phystop:016x}")
            print(f"Physical RAM size:                {(phystop - kernbase) // (1024*1024)} MB")
            
            # Try to get end of kernel binary
            try:
                end = int(gdb.parse_and_eval("end"))
                print(f"Kernel binary end:                0x{end:016x}")
                print(f"Kernel binary size:               {(end - kernbase) // 1024} KB")
            except:
                print("Kernel binary end: [cannot determine]")
            
        except Exception as e:
            print(f"Error reading memory layout: {e}")
    
    def dump_cpus(self):
        """Dump per-CPU structures."""
        print("\n" + "=" * 80)
        print("PER-CPU STRUCTURES (struct cpu cpus[])")
        print("=" * 80)
        
        try:
            cpus = gdb.parse_and_eval("cpus")
            ncpu = int(gdb.parse_and_eval("NCPU")) if self._symbol_exists("NCPU") else 8
            
            for i in range(ncpu):
                cpu = cpus[i]
                
                # Check if CPU is initialized (has a proc or context)
                proc_ptr = int(cpu['proc'])
                noff = int(cpu['noff'])
                intena = int(cpu['intena'])
                
                print(f"\nCPU {i}:")
                print(f"  Current process:    0x{proc_ptr:016x}", end="")
                if proc_ptr != 0:
                    try:
                        proc = cpu['proc'].dereference()
                        print(f" (PID {int(proc['pid'])}: {proc['name'].string()})")
                    except:
                        print(" (cannot dereference)")
                else:
                    print(" (none)")
                
                print(f"  Interrupt disable depth: {noff}")
                print(f"  Interrupts enabled:      {bool(intena)}")
                
                # Try to get scheduler context
                try:
                    context = cpu['context']
                    print(f"  Scheduler context:")
                    print(f"    ra:  0x{int(context['ra']):016x}")
                    print(f"    sp:  0x{int(context['sp']):016x}")
                except:
                    pass
                    
        except Exception as e:
            print(f"Error reading CPU structures: {e}")
    
    def dump_processes(self):
        """Dump process table summary."""
        print("\n" + "=" * 80)
        print("PROCESS TABLE SUMMARY (struct proc proc[])")
        print("=" * 80)
        
        try:
            proc_array = gdb.parse_and_eval("proc")
            nproc = 64
            
            state_names = ["UNUSED", "USED", "SLEEPING", "RUNNABLE", "RUNNING", "ZOMBIE"]
            state_counts = [0] * len(state_names)
            active_procs = []
            
            for i in range(nproc):
                p = proc_array[i]
                state = int(p['state'])
                state_counts[state] += 1
                
                if state != 0:  # Not UNUSED
                    active_procs.append({
                        'index': i,
                        'pid': int(p['pid']),
                        'name': p['name'].string(),
                        'state': state_names[state] if state < len(state_names) else "UNKNOWN",
                        'sz': int(p['sz'])
                    })
            
            print(f"\nTotal process table entries: {nproc}")
            for state_idx, count in enumerate(state_counts):
                if state_idx < len(state_names):
                    print(f"  {state_names[state_idx]:10s}: {count:3d}")
            
            if active_procs:
                print(f"\nActive processes ({len(active_procs)}):")
                print(f"  {'IDX':>4s}  {'PID':>5s}  {'NAME':12s}  {'STATE':10s}  {'SIZE':>8s}")
                print("  " + "-" * 50)
                for p in active_procs:
                    print(f"  {p['index']:4d}  {p['pid']:5d}  {p['name']:12s}  {p['state']:10s}  {p['sz']:8d}")
                    
        except Exception as e:
            print(f"Error reading process table: {e}")
    
    def dump_kmem(self):
        """Dump kernel memory allocator (free page list)."""
        print("\n" + "=" * 80)
        print("KERNEL MEMORY ALLOCATOR (struct kmem)")
        print("=" * 80)
        
        try:
            kmem = gdb.parse_and_eval("kmem")
            freelist = kmem['freelist']
            
            # Count free pages by traversing linked list
            free_pages = 0
            current = freelist
            visited = set()
            
            while int(current) != 0:
                addr = int(current)
                if addr in visited:
                    print(f"WARNING: Circular reference detected in free list at 0x{addr:x}")
                    break
                visited.add(addr)
                
                free_pages += 1
                if free_pages > 100000:  # Safety check
                    print("WARNING: Free list traversal exceeded safety limit")
                    break
                
                # Each free page has pointer to next free page as first 8 bytes
                try:
                    current = current.cast(gdb.lookup_type("struct run").pointer()).dereference()['next']
                except:
                    break
            
            page_size = 4096
            free_memory_mb = (free_pages * page_size) / (1024 * 1024)
            
            print(f"Free list head:      0x{int(freelist):016x}")
            print(f"Free pages:          {free_pages}")
            print(f"Free memory:         {free_memory_mb:.2f} MB")
            print(f"Free memory:         {free_pages * page_size} bytes")
            
        except Exception as e:
            print(f"Error reading kmem structure: {e}")
    
    def dump_bcache(self):
        """Dump buffer cache (disk block cache)."""
        print("\n" + "=" * 80)
        print("BUFFER CACHE (struct bcache)")
        print("=" * 80)
        
        try:
            bcache = gdb.parse_and_eval("bcache")
            
            # Buffer cache has fixed array of buffers
            nbuf = 30  # NBUF constant, typically 30
            
            valid_count = 0
            dirty_count = 0
            busy_count = 0
            
            print(f"Buffer cache contains {nbuf} buffers:")
            print(f"\n  {'IDX':>3s}  {'DEV':>3s}  {'BLOCKNO':>8s}  {'FLAGS':10s}  {'REFCNT':>6s}")
            print("  " + "-" * 45)
            
            for i in range(nbuf):
                buf = bcache['buf'][i]
                
                dev = int(buf['dev'])
                blockno = int(buf['blockno'])
                flags = int(buf['flags'])
                refcnt = int(buf['refcnt'])
                
                # Decode flags (B_VALID=0x2, B_DIRTY=0x4)
                flag_str = ""
                if flags & 0x2:
                    flag_str += "VALID "
                    valid_count += 1
                if flags & 0x4:
                    flag_str += "DIRTY "
                    dirty_count += 1
                if refcnt > 0:
                    flag_str += "BUSY "
                    busy_count += 1
                
                if flags != 0 or refcnt > 0:  # Only show active buffers
                    print(f"  {i:3d}  {dev:3d}  {blockno:8d}  {flag_str:10s}  {refcnt:6d}")
            
            print(f"\nSummary:")
            print(f"  Valid buffers: {valid_count}")
            print(f"  Dirty buffers: {dirty_count}")
            print(f"  Busy buffers:  {busy_count}")
            
        except Exception as e:
            print(f"Error reading buffer cache: {e}")
    
    def dump_icache(self):
        """Dump inode cache."""
        print("\n" + "=" * 80)
        print("INODE CACHE (struct icache)")
        print("=" * 80)
        
        try:
            icache = gdb.parse_and_eval("icache")
            
            ninode = 50  # NINODE constant, typically 50
            
            active_inodes = []
            
            for i in range(ninode):
                inode = icache['inode'][i]
                ref = int(inode['ref'])
                
                if ref > 0:
                    inum = int(inode['inum'])
                    dev = int(inode['dev'])
                    itype = int(inode['type'])
                    
                    # Type mapping (T_DIR=1, T_FILE=2, T_DEVICE=3)
                    type_names = {0: "EMPTY", 1: "DIR", 2: "FILE", 3: "DEVICE"}
                    type_str = type_names.get(itype, f"UNKNOWN({itype})")
                    
                    active_inodes.append({
                        'index': i,
                        'dev': dev,
                        'inum': inum,
                        'ref': ref,
                        'type': type_str
                    })
            
            print(f"Inode cache size: {ninode}")
            print(f"Active inodes: {len(active_inodes)}")
            
            if active_inodes:
                print(f"\n  {'IDX':>3s}  {'DEV':>3s}  {'INUM':>6s}  {'TYPE':8s}  {'REFCNT':>6s}")
                print("  " + "-" * 40)
                for inode in active_inodes:
                    print(f"  {inode['index']:3d}  {inode['dev']:3d}  {inode['inum']:6d}  "
                          f"{inode['type']:8s}  {inode['ref']:6d}")
                    
        except Exception as e:
            print(f"Error reading inode cache: {e}")
    
    def dump_ftable(self):
        """Dump file table (open files)."""
        print("\n" + "=" * 80)
        print("FILE TABLE (struct ftable)")
        print("=" * 80)
        
        try:
            ftable = gdb.parse_and_eval("ftable")
            
            nfile = 100  # NFILE constant, typically 100
            
            active_files = []
            
            for i in range(nfile):
                f = ftable['file'][i]
                ref = int(f['ref'])
                
                if ref > 0:
                    ftype = int(f['type'])
                    readable = bool(int(f['readable']))
                    writable = bool(int(f['writable']))
                    
                    # Type mapping (FD_NONE=0, FD_PIPE=1, FD_INODE=2, FD_DEVICE=3)
                    type_names = {0: "NONE", 1: "PIPE", 2: "INODE", 3: "DEVICE"}
                    type_str = type_names.get(ftype, f"UNKNOWN({ftype})")
                    
                    perms = ""
                    if readable: perms += "R"
                    if writable: perms += "W"
                    
                    active_files.append({
                        'index': i,
                        'type': type_str,
                        'ref': ref,
                        'perms': perms
                    })
            
            print(f"File table size: {nfile}")
            print(f"Active file structures: {len(active_files)}")
            
            if active_files:
                print(f"\n  {'IDX':>3s}  {'TYPE':8s}  {'PERMS':5s}  {'REFCNT':>6s}")
                print("  " + "-" * 30)
                for f in active_files:
                    print(f"  {f['index']:3d}  {f['type']:8s}  {f['perms']:5s}  {f['ref']:6d}")
                    
        except Exception as e:
            print(f"Error reading file table: {e}")
    
    def dump_devsw(self):
        """Dump device switch table."""
        print("\n" + "=" * 80)
        print("DEVICE SWITCH TABLE (struct devsw devsw[])")
        print("=" * 80)
        
        try:
            devsw = gdb.parse_and_eval("devsw")
            
            ndev = 10  # NDEV constant, typically 10
            
            print(f"  {'DEV':>3s}  {'READ':18s}  {'WRITE':18s}")
            print("  " + "-" * 50)
            
            for i in range(ndev):
                dev = devsw[i]
                read_ptr = int(dev['read'])
                write_ptr = int(dev['write'])
                
                if read_ptr != 0 or write_ptr != 0:
                    read_sym = self._addr_to_symbol(read_ptr) if read_ptr != 0 else "null"
                    write_sym = self._addr_to_symbol(write_ptr) if write_ptr != 0 else "null"
                    
                    print(f"  {i:3d}  {read_sym:18s}  {write_sym:18s}")
                    
        except Exception as e:
            print(f"Error reading device switch table: {e}")
    
    def dump_log(self):
        """Dump file system log structure."""
        print("\n" + "=" * 80)
        print("FILE SYSTEM LOG (struct log)")
        print("=" * 80)
        
        try:
            log = gdb.parse_and_eval("log")
            
            start = int(log['start'])
            size = int(log['size'])
            outstanding = int(log['outstanding'])
            committing = int(log['committing'])
            dev = int(log['dev'])
            lh_n = int(log['lh']['n'])
            
            print(f"Log start block:         {start}")
            print(f"Log size (blocks):       {size}")
            print(f"Outstanding transactions: {outstanding}")
            print(f"Committing:              {bool(committing)}")
            print(f"Device:                  {dev}")
            print(f"Log header entries:      {lh_n}")
            
            if lh_n > 0:
                print(f"\nLogged blocks:")
                for i in range(min(lh_n, 30)):  # LOGSIZE is typically 30
                    blockno = int(log['lh']['block'][i])
                    print(f"  [{i:2d}] Block {blockno}")
                    
        except Exception as e:
            print(f"Error reading log structure: {e}")
    
    def _symbol_exists(self, symbol_name):
        """Check if a symbol exists."""
        try:
            gdb.parse_and_eval(symbol_name)
            return True
        except:
            return False
    
    def _addr_to_symbol(self, addr):
        """Convert address to symbol name."""
        try:
            block = gdb.block_for_pc(addr)
            if block and block.function:
                return str(block.function.name)
        except:
            pass
        return f"0x{addr:x}"

# Register the command
DumpKernelCommand()
