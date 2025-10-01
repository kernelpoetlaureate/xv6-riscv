import gdb

class DumpKallocDetailCommand(gdb.Command):
    """Detailed analysis of kernel memory allocator."""
    
    def __init__(self):
        super(DumpKallocDetailCommand, self).__init__("dump_kalloc", gdb.COMMAND_DATA)
    
    def invoke(self, arg, from_tty):
        print("=" * 60)
        print("KERNEL MEMORY ALLOCATOR DETAILED ANALYSIS")
        print("=" * 60)
        
        try:
            kmem = gdb.parse_and_eval("kmem")
            freelist = kmem['freelist']
            
            # Traverse free list and collect addresses
            free_pages = []
            current = freelist
            
            while int(current) != 0 and len(free_pages) < 100000:
                addr = int(current)
                free_pages.append(addr)
                
                try:
                    run_type = gdb.lookup_type("struct run").pointer()
                    current = current.cast(run_type).dereference()['next']
                except:
                    break
            
            print(f"\nTotal free pages: {len(free_pages)}")
            print(f"Free memory: {len(free_pages) * 4096} bytes ({len(free_pages) * 4096 / (1024*1024):.2f} MB)")
            
            # Show first and last few free pages
            print(f"\nFirst 10 free page addresses:")
            for i, addr in enumerate(free_pages[:10]):
                print(f"  [{i}] 0x{addr:016x}")
            
            if len(free_pages) > 10:
                print(f"\nLast 10 free page addresses:")
                for i, addr in enumerate(free_pages[-10:]):
                    print(f"  [{len(free_pages)-10+i}] 0x{addr:016x}")
            
            # Analyze distribution
            if len(free_pages) > 1:
                min_addr = min(free_pages)
                max_addr = max(free_pages)
                print(f"\nAddress range:")
                print(f"  Min: 0x{min_addr:016x}")
                print(f"  Max: 0x{max_addr:016x}")
                print(f"  Span: {(max_addr - min_addr) / (1024*1024):.2f} MB")
                
        except Exception as e:
            print(f"Error: {e}")

DumpKallocDetailCommand()
