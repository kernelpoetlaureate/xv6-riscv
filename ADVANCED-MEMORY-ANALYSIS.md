# Advanced Memory Analysis for XV6-RISCV

This directory contains advanced tools for mapping every XV6 kernel address to exact source file and line number using DWARF debug information. These tools implement cutting-edge memory analysis techniques based on research into `addr2line`, `nm`, `objdump`, and DWARF debugging information.

## 🎯 Overview

The advanced memory analysis system provides:

- **Complete address-to-source mapping**: Map any kernel address to exact source file:line
- **BSS section analysis**: Detailed analysis of global variables and their memory usage
- **Source-interleaved disassembly**: Assembly code with corresponding C source
- **DWARF debug information extraction**: Complete debug symbol analysis
- **Memory usage attribution**: Track memory usage by source file and variable
- **Automated analysis workflows**: Scripts for comprehensive kernel analysis

## ⚡ Key Features

### 1. Address-to-Source Translation
- Uses `addr2line` to convert memory addresses to source locations
- Enhanced with function names (`-f`), pretty printing (`-p`), and inline info (`-i`)
- Supports batch processing of multiple addresses

### 2. Symbol Table Analysis with Source Attribution
- Uses `nm -l` to annotate symbols with source file:line information
- Uses `nm -S` to include symbol sizes for memory usage analysis
- Categorizes symbols by type (functions, global variables, etc.)

### 3. Source Code Interleaving
- Uses `objdump -S -l` to interleave C source with assembly disassembly
- Requires debug symbols (`-g` compilation flag)
- Shows exact correspondence between source lines and machine instructions

### 4. DWARF Debug Information Extraction  
- Uses `objdump --dwarf=decodedline` for complete instruction-to-source mapping
- Extracts compilation units, debug information entries (DIEs), and line number programs
- Provides comprehensive symbol-to-source attribution

## 🔧 Tools

### 1. Enhanced DWARF Extractor (`scripts/dwarf-extractor.py`)

Advanced Python script for comprehensive DWARF analysis:

```bash
# Basic DWARF info extraction
python3 scripts/dwarf-extractor.py kernel/kernel

# Generate all analysis files
python3 scripts/dwarf-extractor.py kernel/kernel --all

# Focus on BSS section analysis
python3 scripts/dwarf-extractor.py kernel/kernel --bss-analysis

# Generate comprehensive memory analysis
python3 scripts/dwarf-extractor.py kernel/kernel --comprehensive
```

**Features:**
- Maps BSS symbols to source code declarations
- Generates JSON data for machine processing
- Calculates memory usage by source file
- Creates human-readable analysis reports

### 2. Address-to-Source Analyzer (`scripts/addr2line-analyzer.sh`)

Comprehensive bash script implementing research-based analysis techniques:

```bash
# Basic BSS and source mapping analysis
./scripts/addr2line-analyzer.sh

# Generate all analysis files
./scripts/addr2line-analyzer.sh --all

# Focus on specific analysis types
./scripts/addr2line-analyzer.sh --bss --source-map --disasm
```

**Features:**
- BSS symbol to source code mapping
- Complete symbol table with source locations
- DWARF line number program extraction
- Source-interleaved disassembly generation
- Memory usage analysis by source file

### 3. Symbol Analyzer (`scripts/symbol-analyzer.py`)

Enhanced symbol table analysis with memory layout:

```bash
python3 scripts/symbol-analyzer.py kernel/kernel -o kernel-analysis.sym
```

### 4. Demonstration Script (`demo-advanced-analysis.sh`)

Shows practical usage of all tools:

```bash
./demo-advanced-analysis.sh
```

## 📊 Analysis Outputs

### Memory Layout with Source Attribution
```
Address            Size    Symbol           Source Location
------------------------------------------------------------------------
0x8000a230         4 B     panicked         kernel/printf.c:15
0x8000a234         4 B     panicking        kernel/printf.c:16  
0x8000a248         8 B     kernel_pagetable kernel/vm.c:13
0x8000a250         8 B     initproc         kernel/proc.c:8
0x8000a258         4 B     ticks            kernel/trap.c:95
0x80012788         23 KB   proc[NPROC]      kernel/proc.c:9
0x800181a0         34 KB   bcache           kernel/bio.c:26
```

### BSS Section Analysis
```json
{
  "bss_globals": [
    {
      "name": "bcache",
      "address": "0x800181a0", 
      "size": 35360,
      "source_file": "kernel/bio.c",
      "source_line": "26",
      "section": ".bss"
    }
  ]
}
```

### Source-Interleaved Disassembly
```assembly
kernel/bio.c:26
00000000800181a0 <bcache>:
struct {
  struct spinlock lock;
  struct buf buf[NBUF];
} bcache;
  800181a0: 00 00 00 00   .word 0x00000000
```

## 🚀 Quick Start

1. **Build the kernel with debug information:**
   ```bash
   make clean
   make kernel/kernel
   ```

2. **Run comprehensive analysis:**
   ```bash
   ./demo-advanced-analysis.sh
   ```

3. **Examine generated files:**
   - `comprehensive-memory-analysis.txt` - Main human-readable report
   - `comprehensive-memory-analysis.txt.json` - Machine-readable data
   - `analysis-output/` - Detailed analysis files

## 💡 Practical Use Cases

### 1. Memory Corruption Debugging
When you have a kernel panic at address `0x800181a0`:

```bash
# Find the symbol at this address  
nm kernel/kernel | awk '$1 <= "800181a0" {name=$3; addr=$1} END {print addr, name}'

# Get exact source location
addr2line -e kernel/kernel -f -p 0x800181a0
# Output: bcache at kernel/bio.c:26

# Examine the source code
sed -n '20,30p' kernel/bio.c
```

### 2. Memory Usage Optimization
Identify the largest BSS variables:

```bash
# Find largest BSS symbols
nm -S kernel/kernel | grep ' B ' | sort -k2 -nr | head -10

# Map each to source location
for addr in $(nm kernel/kernel | grep ' B ' | head -5 | cut -d' ' -f1); do
    echo -n "$addr: "
    addr2line -e kernel/kernel -f -p 0x$addr
done
```

### 3. Code Understanding
Map all global variables to their source declarations:

```bash
python3 scripts/dwarf-extractor.py kernel/kernel --comprehensive
# Generates complete mapping in comprehensive-memory-analysis.txt
```

## 🔬 Advanced Techniques

### Manual Address Investigation
```bash
# Get symbol table with source lines
nm -l kernel/kernel > symbols-with-sources.txt

# Get symbol sizes
nm -S kernel/kernel > symbols-with-sizes.txt

# Get DWARF line mappings
objdump --dwarf=decodedline kernel/kernel > dwarf-line-map.txt

# Generate source-interleaved disassembly
objdump -S -l kernel/kernel > source-disassembly.txt
```

### Batch Address Analysis
```bash
# Create file with addresses to analyze
echo -e "0x800181a0\n0x80012788\n0x8000a250" > addresses.txt

# Batch analyze with addr2line
addr2line -e kernel/kernel -f -p -i $(cat addresses.txt)
```

### Custom Analysis Scripts
```bash
#!/bin/bash
# Find all BSS variables larger than 1KB
nm -S kernel/kernel | grep ' B ' | while read addr size type name; do
    size_bytes=$((0x$size))
    if [ $size_bytes -gt 1024 ]; then
        source_loc=$(addr2line -e kernel/kernel -f -p "0x$addr")
        echo "$name: $size_bytes bytes at $source_loc"
    fi
done
```

## 📋 Requirements

- **Binutils tools**: `nm`, `addr2line`, `objdump`, `readelf`
- **Python 3** for enhanced analysis scripts
- **Bash** for automation scripts
- **XV6 kernel compiled with debug information** (`-g` flag)

## 🏗️ Technical Implementation

### DWARF Debug Information
The tools leverage DWARF debugging information compiled into the kernel:
- **`.debug_info`**: Compilation units and data structure definitions
- **`.debug_line`**: Line number programs mapping addresses to source lines
- **`.debug_abbrev`**: Abbreviation tables for compact debug info storage

### Tool Chain Integration
- **`nm`**: Symbol table extraction with source annotations (`-l`) and sizes (`-S`)
- **`addr2line`**: Address-to-source translation with function context (`-f -p -i`)
- **`objdump`**: Disassembly with source interleaving (`-S -l`) and DWARF extraction (`-W`, `--dwarf=*`)

### Data Processing Pipeline
1. Extract raw symbol and debug information
2. Cross-reference addresses with source locations
3. Calculate memory usage and attribution
4. Generate human-readable and machine-readable reports

## 📚 Research Background

This implementation is based on comprehensive research into DWARF debugging information and memory analysis techniques. The tools implement advanced methods for:

- Complete instruction-to-source-line mapping using DWARF line number programs
- Symbol table analysis with source attribution
- Memory layout visualization with source code correlation
- Automated debugging workflows for kernel development

## 🔍 Troubleshooting

### Missing Debug Information
If analysis fails, ensure the kernel is compiled with debug symbols:
```bash
readelf -S kernel/kernel | grep debug_
# Should show .debug_info, .debug_line, etc.
```

### Tool Dependencies
Install required tools on Ubuntu/Debian:
```bash
sudo apt-get install binutils python3
```

### Address Translation Failures
If `addr2line` fails, check:
1. Kernel binary exists and is not stripped
2. Address format is correct (hexadecimal with 0x prefix)
3. Debug information is present in the binary

## 📈 Performance Notes

- Analysis time scales with kernel size and debug information complexity
- Large disassembly files may require significant disk space
- Batch address translation is more efficient than individual lookups
- JSON output enables integration with other analysis tools

---

*This advanced memory analysis system provides comprehensive tools for understanding XV6 kernel memory layout, debugging memory issues, and optimizing memory usage through precise source code attribution.*