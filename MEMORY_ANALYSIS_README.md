# Comprehensive Memory Analysis Tool for RISC-V xv6

This tool provides exhaustive memory examination capabilities for QEMU-emulated RISC-V systems, specifically targeting xv6 operating system debugging scenarios.

## Features

### Advanced Memory Introspection Capabilities
- **Multi-layered memory access strategies** through GDB's inferior interface
- **Byte-level granularity** with complete coverage of addressable space
- **Real-time interactive analysis interface** with live memory browsing
- **Memory region discovery** covering RAM, UART, CLINT, PLIC regions
- **Pattern matching** across entire address space with chunked searching
- **Comprehensive memory map reporting** with symbol resolution

## Installation & Usage

### Prerequisites
- RISC-V xv6 system compiled and running under QEMU
- GDB with Python support
- QEMU running with GDB server enabled

### Quick Start

1. **Start xv6 with debugging enabled:**
   ```bash
   make qemu-gdb
   ```

2. **In another terminal, connect GDB:**
   ```bash
   gdb-multiarch kernel/kernel
   ```

3. **Connect to QEMU:**
   ```gdb
   (gdb) target remote localhost:26000
   ```

4. **Load the memory analysis tool:**
   ```gdb
   (gdb) source memory_analysis.gdb
   ```
   
   Or alternatively:
   ```gdb
   (gdb) python exec(open('full.py').read())
   ```

## Available Commands

### Comprehensive Memory Analysis Commands

#### `mem_analysis dump-all`
Dumps all major memory regions to binary files in `./memory_dumps/` directory.

**Example:**
```gdb
(gdb) mem_analysis dump-all
=== Comprehensive Memory Dump ===

Dumping Main RAM (0x80000000-0x88000000)...
  Saved 134217728 bytes to ./memory_dumps/memory_dump_main_ram_80000000.bin
  
Dumping UART (0x10000000-0x10001000)...
  Saved 4096 bytes to ./memory_dumps/memory_dump_uart_10000000.bin
```

#### `mem_analysis dump-range <start> <end>`
Dumps a specific address range with hex preview.

**Example:**
```gdb
(gdb) mem_analysis dump-range 0x80000000 0x80001000
=== Memory Range Dump 0x80000000-0x80001000 ===
Saved 4096 bytes to ./memory_dumps/memory_range_80000000_80001000.bin
0x80000000  6f 00 00 ef 73 00 00 00 73 00 00 00 73 00 00 00  |o...s...s...s...|
0x80000010  73 00 00 00 73 00 00 00 73 00 00 00 73 00 00 00  |s...s...s...s...|
```

#### `mem_analysis search <hex_pattern>`
Searches for a hex pattern across all accessible memory regions.

**Example:**
```gdb
(gdb) mem_analysis search deadbeef
=== Searching for pattern 'deadbeef' ===
Found 2 matches:
  0x80001234 in Main RAM
    Context:
    0x80001228  ca fe ba be de ad be ef 12 34 56 78 9a bc de f0  |.........4Vx....|
  0x87005678 in Main RAM
```

#### `mem_analysis interactive`
Starts an interactive memory browser for real-time exploration.

**Example:**
```gdb
(gdb) mem_analysis interactive
=== Interactive Memory Browser ===
Commands: read <addr> [size], search <pattern>, regions, quit
mem> read 0x80000000 64
0x80000000  6f 00 00 ef 73 00 00 00 73 00 00 00 73 00 00 00  |o...s...s...s...|
0x80000010  73 00 00 00 73 00 00 00 73 00 00 00 73 00 00 00  |s...s...s...s...|
mem> search cafebabe
  0x87001234 in Main RAM
mem> quit
```

#### `mem_analysis report`
Generates a comprehensive memory map report with system information.

#### `mem_analysis regions`
Lists all known memory regions with access permissions.

### Quick Access Commands

#### `memdump <addr> [size]`
Quick hex dump of memory at specified address.

**Example:**
```gdb
(gdb) memdump 0x80000000 128
Memory dump at 0x80000000 (128 bytes):
0x80000000  6f 00 00 ef 73 00 00 00 73 00 00 00 73 00 00 00  |o...s...s...s...|
0x80000010  73 00 00 00 73 00 00 00 73 00 00 00 73 00 00 00  |s...s...s...s...|
```

#### `memsearch <hex_pattern> [start] [end]`
Quick pattern search in specified memory range.

**Example:**
```gdb
(gdb) memsearch cafebabe 0x80000000 0x88000000
Searching for pattern 'cafebabe' in 0x80000000-0x88000000...
Found 1 matches:
  0x87001234
```

## Memory Regions Covered

| Start Address | End Address | Description |
|---------------|-------------|-------------|
| 0x80000000 | 0x88000000 | Main RAM (128MB) |
| 0x10000000 | 0x10001000 | UART0 Registers |
| 0x02000000 | 0x02010000 | CLINT (Core Local Interruptor) |
| 0x0c000000 | 0x0c400000 | PLIC (Platform Level Interrupt Controller) |

## Output Files

All memory dumps are saved to the `./memory_dumps/` directory with descriptive filenames:
- `memory_dump_main_ram_80000000.bin` - Full RAM dump
- `memory_dump_uart_10000000.bin` - UART register dump  
- `memory_range_START_END.bin` - Custom range dumps

## Error Handling

The tool includes comprehensive error handling for:
- Memory access violations
- Unmapped memory regions
- Privilege level restrictions
- Connection failures

## Integration with Standard Tools

Dump files are in standard binary format and can be analyzed with:
- **Hex editors**: `hexdump -C`, `xxd`, `ghex`
- **Disassemblers**: `objdump`, `radare2`
- **Binary analysis tools**: `binwalk`, `strings`

## Troubleshooting

### "No module named 'telnetlib'" Error
This has been fixed in the current version. The tool now uses pure socket connections.

### "Undefined command" Error  
Make sure to use `source memory_analysis.gdb` or `python exec(open('full.py').read())` to load the tool.

### Memory Access Errors
Some memory regions may not be accessible depending on the current execution state. The tool will report these gracefully and continue with accessible regions.

## Examples Session

```bash
# Terminal 1: Start xv6 with debugging
$ make qemu-gdb

# Terminal 2: GDB session
$ gdb-multiarch kernel/kernel
(gdb) target remote localhost:26000
(gdb) source memory_analysis.gdb

# Explore memory
(gdb) mem_analysis regions
(gdb) memdump 0x80000000 256
(gdb) mem_analysis search 48656c6c6f  # Search for "Hello"
(gdb) mem_analysis interactive

# Generate comprehensive report
(gdb) mem_analysis report > memory_report.txt
```

This tool provides complete visibility into the xv6 system's memory space, enabling thorough analysis of kernel behavior and system state without any hidden memory regions or access limitations.