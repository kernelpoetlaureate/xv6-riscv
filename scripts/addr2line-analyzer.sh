#!/bin/bash

# Advanced Address-to-Source Analyzer for XV6-RISCV
# Implements comprehensive memory analysis using addr2line, nm, and objdump
# Based on research into DWARF debug information tools

KERNEL_FILE="kernel/kernel"
OUTPUT_DIR="analysis-output"

# Colors for output formatting
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Advanced memory analysis for XV6-RISCV kernel using DWARF debug information"
    echo ""
    echo "OPTIONS:"
    echo "  -k, --kernel FILE     Kernel binary file (default: kernel/kernel)"
    echo "  -o, --output DIR      Output directory (default: analysis-output)"
    echo "  -a, --all            Generate all analysis files"
    echo "  -b, --bss            Analyze BSS section symbols"
    echo "  -s, --source-map     Generate source code mappings"
    echo "  -d, --disasm         Generate source-interleaved disassembly"
    echo "  -l, --line-map       Extract complete DWARF line mappings"
    echo "  -h, --help           Show this help message"
    echo ""
    echo "EXAMPLES:"
    echo "  $0 --all                           # Generate all analysis files"
    echo "  $0 --bss --source-map             # Focus on BSS and source mapping"
    echo "  $0 --kernel custom/kernel --all   # Analyze custom kernel binary"
}

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_dependencies() {
    local missing_deps=()
    
    for cmd in nm addr2line objdump readelf; do
        if ! command -v "$cmd" &> /dev/null; then
            missing_deps+=("$cmd")
        fi
    done
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        log_error "Missing required dependencies: ${missing_deps[*]}"
        echo "Please install binutils package"
        exit 1
    fi
}

verify_kernel_file() {
    if [ ! -f "$KERNEL_FILE" ]; then
        log_error "Kernel file not found: $KERNEL_FILE"
        echo "Please build the kernel first with: make kernel/kernel"
        exit 1
    fi
    
    # Check if kernel has debug information
    if ! readelf -S "$KERNEL_FILE" | grep -q "debug_"; then
        log_warning "Kernel may not contain DWARF debug information"
        echo "For best results, ensure kernel is compiled with -g flag"
    fi
}

create_output_dir() {
    mkdir -p "$OUTPUT_DIR"
    log_info "Output directory: $OUTPUT_DIR"
}

map_bss_to_source() {
    log_info "Mapping BSS symbols to source code locations..."
    
    local output_file="$OUTPUT_DIR/bss-symbols-to-source.txt"
    
    {
        echo "# BSS Symbol to Source Code Mapping"
        echo "# Generated on $(date)"
        echo "# Kernel: $KERNEL_FILE"
        echo "# Maps each BSS global variable to its source file and line number"
        echo ""
        echo "Symbol                         Address          Size      Source Location"
        echo "=============================================================================="
        
        # Get BSS symbols with line numbers using nm -l
        nm -l "$KERNEL_FILE" | grep ' [Bb] ' | while read addr type name source; do
            # Get enhanced source location using addr2line
            if [ -n "$addr" ]; then
                addr_clean="0x$addr"
                detailed_source=$(addr2line -e "$KERNEL_FILE" -f -p "$addr_clean" 2>/dev/null)
                
                # Get symbol size if available
                size_info=$(nm -S "$KERNEL_FILE" | grep "^$addr " | awk '{print $2}')
                if [ -n "$size_info" ]; then
                    size_bytes=$((0x$size_info))
                    if [ $size_bytes -gt 1024 ]; then
                        size_display="${size_bytes} B ($(echo "scale=1; $size_bytes/1024" | bc) KB)"
                    else
                        size_display="${size_bytes} B"
                    fi
                else
                    size_display="unknown"
                fi
                
                printf "%-30s %s %12s %s\n" "$name" "$addr_clean" "$size_display" "$detailed_source"
            fi
        done
        
    } > "$output_file"
    
    log_success "BSS mapping written to: $output_file"
}

generate_complete_symbol_map() {
    log_info "Generating complete symbol to source mapping..."
    
    local output_file="$OUTPUT_DIR/complete-symbol-map.txt"
    
    {
        echo "# Complete Symbol Table with Source Locations"
        echo "# Generated on $(date)"
        echo "# Kernel: $KERNEL_FILE"
        echo ""
        echo "=== MEMORY LAYOUT OVERVIEW ==="
        objdump -h "$KERNEL_FILE" | grep -E '^\s*[0-9]+\s+\.' | \
            awk '{printf "%-15s VMA: %-16s Size: %-10s LMA: %s\n", $2, $3, $4, $5}'
        echo ""
        
        echo "=== ALL SYMBOLS WITH SOURCE LOCATIONS ==="
        echo "Address          Type Size     Symbol                         Source"
        echo "==============================================================================="
        
        # Use nm with multiple flags for comprehensive information
        nm -l -S -n "$KERNEL_FILE" | while read line; do
            # Parse nm output: address [size] type name [source]
            if echo "$line" | grep -q '^[0-9a-f]'; then
                addr=$(echo "$line" | awk '{print $1}')
                
                # Determine if size is present
                if echo "$line" | awk '{print $2}' | grep -q '^[0-9a-f]'; then
                    # Size is present
                    size=$(echo "$line" | awk '{print $2}')
                    type=$(echo "$line" | awk '{print $3}')
                    name=$(echo "$line" | awk '{print $4}')
                    source=$(echo "$line" | cut -d' ' -f5-)
                    size_bytes=$((0x$size))
                    size_display=$(printf "%6d" $size_bytes)
                else
                    # No size
                    size_display="     ?" 
                    type=$(echo "$line" | awk '{print $2}')
                    name=$(echo "$line" | awk '{print $3}')
                    source=$(echo "$line" | cut -d' ' -f4-)
                fi
                
                printf "0x%-14s %s %s %-30s %s\n" "$addr" "$type" "$size_display" "$name" "$source"
            fi
        done
        
    } > "$output_file"
    
    log_success "Complete symbol map written to: $output_file"
}

extract_dwarf_line_mapping() {
    log_info "Extracting complete DWARF line number mappings..."
    
    local output_file="$OUTPUT_DIR/dwarf-line-mappings.txt"
    
    {
        echo "# DWARF Line Number Program Mappings"
        echo "# Generated on $(date)"
        echo "# Kernel: $KERNEL_FILE"
        echo "# Complete mapping between machine addresses and source lines"
        echo ""
        
        # Extract decoded line information
        objdump --dwarf=decodedline "$KERNEL_FILE" 2>/dev/null || {
            log_warning "objdump --dwarf=decodedline not supported, using alternative method"
            
            echo "=== LINE NUMBER STATEMENTS ==="
            objdump --dwarf=line "$KERNEL_FILE" | grep -E '(File name|Line Number|Address)'
        }
        
    } > "$output_file"
    
    log_success "DWARF line mappings written to: $output_file"
}

generate_source_interleaved_disassembly() {
    log_info "Generating source-interleaved disassembly..."
    
    local output_file="$OUTPUT_DIR/source-disassembly.txt"
    
    {
        echo "# Source Code Interleaved with Assembly"
        echo "# Generated on $(date)"
        echo "# Kernel: $KERNEL_FILE"
        echo "# Shows C source code alongside corresponding assembly instructions" 
        echo ""
        
        # Generate disassembly with source code using objdump -S -l
        objdump -S -l "$KERNEL_FILE"
        
    } > "$output_file"
    
    log_success "Source-interleaved disassembly written to: $output_file"
}

analyze_memory_usage() {
    log_info "Analyzing memory usage by source file..."
    
    local output_file="$OUTPUT_DIR/memory-usage-analysis.txt"
    
    {
        echo "# Memory Usage Analysis by Source File"
        echo "# Generated on $(date)"
        echo "# Kernel: $KERNEL_FILE"
        echo ""
        
        echo "=== BSS SECTION ANALYSIS ==="
        total_bss=0
        declare -A file_usage
        
        # Analyze BSS symbols
        nm -S -l "$KERNEL_FILE" | grep ' [Bb] ' | while read addr size type name source; do
            if [ -n "$size" ] && [ "$size" != "0" ]; then
                size_bytes=$((0x$size))
                total_bss=$((total_bss + size_bytes))
                
                # Extract source file
                source_file=$(echo "$source" | cut -d':' -f1)
                if [ -n "$source_file" ]; then
                    file_usage["$source_file"]=$((${file_usage["$source_file"]} + $size_bytes))
                fi
                
                printf "%-20s %8d bytes  %s\n" "$name" "$size_bytes" "$source"
            fi
        done
        
        echo ""
        echo "Total BSS section size: $total_bss bytes"
        
        echo ""
        echo "=== MEMORY USAGE BY SOURCE FILE ==="
        for file in "${!file_usage[@]}"; do
            printf "%-30s %8d bytes\n" "$file" "${file_usage[$file]}"
        done | sort -k2 -nr
        
    } > "$output_file"
    
    log_success "Memory usage analysis written to: $output_file"
}

generate_practical_examples() {
    log_info "Generating practical usage examples..."
    
    local output_file="$OUTPUT_DIR/practical-examples.txt" 
    
    {
        echo "# Practical Examples: Address-to-Source Mapping"
        echo "# Generated on $(date)"
        echo "# Kernel: $KERNEL_FILE"
        echo ""
        
        echo "=== EXAMPLE 1: Finding bcache Variable ==="
        bcache_addr=$(nm "$KERNEL_FILE" | grep ' bcache$' | awk '{print $1}')
        if [ -n "$bcache_addr" ]; then
            echo "1. Find bcache address:"
            echo "   $ nm kernel/kernel | grep bcache"
            echo "   ${bcache_addr} B bcache"
            echo ""
            echo "2. Get source location:"
            echo "   $ addr2line -e kernel/kernel -f -p 0x${bcache_addr}"
            bcache_source=$(addr2line -e "$KERNEL_FILE" -f -p "0x${bcache_addr}" 2>/dev/null)
            echo "   ${bcache_source}"
            echo ""
        fi
        
        echo "=== EXAMPLE 2: Function Address Mapping ==="
        main_addr=$(nm "$KERNEL_FILE" | grep ' main$' | awk '{print $1}')
        if [ -n "$main_addr" ]; then
            echo "1. Find main function address:"
            echo "   $ nm kernel/kernel | grep ' main$'"
            echo "   ${main_addr} T main"
            echo ""
            echo "2. Get detailed source location:"
            echo "   $ addr2line -e kernel/kernel -f -p -i 0x${main_addr}"
            main_source=$(addr2line -e "$KERNEL_FILE" -f -p -i "0x${main_addr}" 2>/dev/null)
            echo "   ${main_source}"
            echo ""
        fi
        
        echo "=== EXAMPLE 3: Automated BSS Analysis Script ==="
        cat << 'EOF'
#!/bin/bash
# Quick BSS analysis script
echo "BSS Symbol Analysis:"
nm kernel/kernel | grep ' [Bb] ' | while read addr type name; do
    source_loc=$(addr2line -e kernel/kernel -f -p "0x$addr" 2>/dev/null)
    printf "%-20s %s\n" "$name" "$source_loc"
done
EOF
        
        echo ""
        echo "=== EXAMPLE 4: Memory Corruption Debugging ==="
        echo "When you have a memory corruption at address 0x12345678:"
        echo "1. Find the symbol:"
        echo "   $ nm kernel/kernel | awk '\$1 <= \"12345678\" {name=\$3; addr=\$1} END {print addr, name}'"
        echo "2. Get exact source location:"
        echo "   $ addr2line -e kernel/kernel -f -p 0x12345678"
        echo "3. View surrounding source code with context"
        
    } > "$output_file"
    
    log_success "Practical examples written to: $output_file"
}

main() {
    local do_all=false
    local do_bss=false
    local do_source_map=false  
    local do_disasm=false
    local do_line_map=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -k|--kernel)
                KERNEL_FILE="$2"
                shift 2
                ;;
            -o|--output)
                OUTPUT_DIR="$2"
                shift 2
                ;;
            -a|--all)
                do_all=true
                shift
                ;;
            -b|--bss)
                do_bss=true
                shift
                ;;
            -s|--source-map)
                do_source_map=true
                shift
                ;;
            -d|--disasm)
                do_disasm=true
                shift
                ;;
            -l|--line-map)
                do_line_map=true
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            *)
                log_error "Unknown option: $1"
                usage
                exit 1
                ;;
        esac
    done
    
    # If no specific options, default to basic analysis
    if [ "$do_all" = false ] && [ "$do_bss" = false ] && [ "$do_source_map" = false ] && [ "$do_disasm" = false ] && [ "$do_line_map" = false ]; then
        do_bss=true
        do_source_map=true
    fi
    
    echo "XV6-RISCV Advanced Address-to-Source Analyzer"
    echo "=============================================="
    
    check_dependencies
    verify_kernel_file
    create_output_dir
    
    if [ "$do_all" = true ] || [ "$do_bss" = true ]; then
        map_bss_to_source
    fi
    
    if [ "$do_all" = true ] || [ "$do_source_map" = true ]; then
        generate_complete_symbol_map
    fi
    
    if [ "$do_all" = true ] || [ "$do_disasm" = true ]; then
        generate_source_interleaved_disassembly
    fi
    
    if [ "$do_all" = true ] || [ "$do_line_map" = true ]; then
        extract_dwarf_line_mapping
    fi
    
    if [ "$do_all" = true ]; then
        analyze_memory_usage
        generate_practical_examples
    fi
    
    echo ""
    log_success "Analysis complete! Results in: $OUTPUT_DIR"
    
    # Summary
    echo ""
    echo "Generated files:"
    ls -la "$OUTPUT_DIR"/ | grep -v '^d' | awk '{print "  " $9 " (" $5 " bytes)"}'
}

# Run main function with all arguments
main "$@"