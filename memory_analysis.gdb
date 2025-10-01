# GDB initialization script for comprehensive memory analysis
# Usage: (gdb) source memory_analysis.gdb

# Load the comprehensive memory analysis Python module
python exec(open('full.py').read())

# Print usage reminder
echo \n=== Comprehensive Memory Analysis Tool Loaded ===\n
echo Available commands:\n
echo   mem_analysis dump-all                    # Dump all memory regions\n
echo   mem_analysis dump-range 0x80000000 0x80001000  # Dump specific range\n
echo   mem_analysis search deadbeef             # Search for hex pattern\n
echo   mem_analysis interactive                 # Start interactive browser\n
echo   mem_analysis report                      # Generate memory map report\n
echo   mem_analysis regions                     # List memory regions\n
echo \n
echo   memdump 0x80000000 256                  # Quick 256-byte hex dump\n
echo   memsearch cafebabe 0x80000000 0x88000000 # Search in RAM region\n
echo \n
echo For detailed help: mem_analysis\n
echo ================================================\n