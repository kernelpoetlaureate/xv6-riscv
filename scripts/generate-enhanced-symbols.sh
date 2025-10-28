#!/bin/bash

# Enhanced symbol file generation script for xv6-riscv kernel
KERNEL_FILE="kernel/kernel"
OUTPUT_FILE="kernel-enhanced.sym"

echo "Generating enhanced symbol file..."

# Create comprehensive symbol file with multiple sections
{
    echo "# XV6-RISCV Enhanced Symbol Table"
    echo "# Generated on $(date)"
    echo "# Kernel: $KERNEL_FILE"
    echo ""
    
    echo "=== MEMORY LAYOUT ==="
    echo "# Address ranges and sections"
    objdump -h "$KERNEL_FILE" | grep -E '^\s*[0-9]+\s+\.' | \
        awk '{printf "%-15s %s %8s %s\n", $2, $3, $4, $5}'
    echo ""
    
    echo "=== FUNCTION SYMBOLS (with sizes) ==="
    echo "# Format: Address Size Type Name Source"
    objdump -t "$KERNEL_FILE" | \
        grep -E '\s+[gG]\s+\*?[FfT]\s+' | \
        sort -k1,1 | \
        while read line; do
            addr=$(echo "$line" | awk '{print $1}')
            size=$(echo "$line" | awk '{print $5}')
            type=$(echo "$line" | awk '{print $4}')
            name=$(echo "$line" | awk '{print $6}')
            # Try to get source file info
            src=$(objdump -l "$KERNEL_FILE" | grep -A1 "$addr" | tail -1 | sed 's/.*\///')
            printf "%-16s %-8s %-4s %-30s %s\n" "$addr" "$size" "$type" "$name" "$src"
        done
    echo ""
    
    echo "=== DATA SYMBOLS ==="
    echo "# Global and static variables"
    objdump -t "$KERNEL_FILE" | \
        grep -E '\s+[gG]\s+\*?[OoDdBbVv]\s+' | \
        sort -k1,1 | \
        awk '{printf "%-16s %-8s %-4s %s\n", $1, $5, $4, $6}'
    echo ""
    
    echo "=== DISASSEMBLY WITH SOURCE ==="
    echo "# Key functions with assembly and source correlation"
    objdump -S "$KERNEL_FILE" | head -200
    echo ""
    
    echo "=== SYMBOL STATISTICS ==="
    echo "Function count: $(objdump -t "$KERNEL_FILE" | grep -c '\s[FfT]\s')"
    echo "Variable count: $(objdump -t "$KERNEL_FILE" | grep -c '\s[OoDdBbVv]\s')"
    echo "Total symbols: $(objdump -t "$KERNEL_FILE" | grep -c '\s[A-Za-z]\s')"
    
} > "$OUTPUT_FILE"

echo "Enhanced symbol file generated: $OUTPUT_FILE"