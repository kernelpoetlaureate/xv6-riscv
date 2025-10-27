# How to Analyze xv6 ELF Files with ELFInsight

## Quick Start

Since ELFInsight doesn't automatically associate with files without extensions, use the **Command Palette**:

1. Press `Ctrl+Shift+P`
2. Type: `ELFInsight: Open ELF File`
3. Select one of these files:
   - `kernel/kernel` - Main xv6 kernel
   - `user/_ps` - Process viewer utility
   - `user/_htop` - System monitor
   - `user/_kinspect` - Kernel inspector

## Alternative: Use Command Line Tools

If ELFInsight continues having issues, you can analyze using these commands:

```bash
# View ELF header
readelf -h kernel/kernel

# List all sections
readelf -S kernel/kernel

# View all symbols (functions and variables)
readelf -s kernel/kernel | less

# Search for a specific symbol
readelf -s kernel/kernel | grep scheduler

# View program headers (segments)
readelf -l kernel/kernel

# Disassemble the kernel
objdump -d kernel/kernel | less

# View specific function
objdump -d kernel/kernel | grep -A 50 '<main>:'
```

## What to Look For

- **Entry Point**: `0x80000000` - where kernel starts
- **Symbols**: All your kernel functions are visible since it's not stripped
- **Sections**: `.text` (code), `.data` (initialized data), `.bss` (uninitialized)
