import gdb

class DumpCPUDetailCommand(gdb.Command):
    """Dump detailed information for a specific CPU."""
    
    def __init__(self):
        super(DumpCPUDetailCommand, self).__init__("dump_cpu", gdb.COMMAND_DATA)
    
    def invoke(self, arg, from_tty):
        args = gdb.string_to_argv(arg)
        if len(args) < 1:
            # Dump all CPUs
            ncpu = 8
            for i in range(ncpu):
                try:
                    self.dump_cpu(i)
                except:
                    break
        else:
            cpuid = int(args[0])
            self.dump_cpu(cpuid)
    
    def dump_cpu(self, cpuid):
        print("=" * 60)
        print(f"CPU {cpuid} DETAILED STATE")
        print("=" * 60)
        
        try:
            cpus = gdb.parse_and_eval("cpus")
            cpu = cpus[cpuid]
            
            # Current process
            proc_ptr = int(cpu['proc'])
            print(f"\nCurrent process: 0x{proc_ptr:016x}")
            if proc_ptr != 0:
                proc = cpu['proc'].dereference()
                print(f"  PID:   {int(proc['pid'])}")
                print(f"  Name:  {proc['name'].string()}")
                print(f"  State: {int(proc['state'])}")
            
            # Interrupt state
            print(f"\nInterrupt state:")
            print(f"  noff (disable depth): {int(cpu['noff'])}")
            print(f"  intena (enabled):     {bool(int(cpu['intena']))}")
            
            # Scheduler context
            print(f"\nScheduler context:")
            context = cpu['context']
            print(f"  ra:  0x{int(context['ra']):016x}")
            print(f"  sp:  0x{int(context['sp']):016x}")
            print(f"  s0:  0x{int(context['s0']):016x}")
            print(f"  s1:  0x{int(context['s1']):016x}")
            
        except Exception as e:
            print(f"Error: {e}")

DumpCPUDetailCommand()
