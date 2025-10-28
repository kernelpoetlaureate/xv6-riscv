#!/bin/bash

# XV6-RISCV Advanced Memory Analysis Demonstration
# Shows practical usage of the enhanced DWARF debug information tools

echo "XV6-RISCV Advanced Memory Analysis Demonstration"
echo "================================================"
echo ""

# Check if kernel exists
KERNEL_FILE="kernel/kernel"
if [ ! -f "$KERNEL_FILE" ]; then
    echo "❌ Kernel file not found: $KERNEL_FILE"
    echo "Please build the kernel first:"
    echo "  make clean"
    echo "  make kernel/kernel"
    exit 1
fi

echo "✅ Found kernel binary: $KERNEL_FILE"
echo ""

# Check if kernel has debug information
if readelf -S "$KERNEL_FILE" | grep -q "debug_"; then
    echo "✅ Kernel contains DWARF debug information"
else
    echo "⚠️  Kernel may not contain complete debug information"
    echo "For best results, ensure kernel is compiled with -g flag"
fi
echo ""

echo "🔍 DEMONSTRATION 1: Basic BSS Symbol Analysis"
echo "---------------------------------------------"
echo "Finding bcache variable location:"
echo ""

# Find bcache using nm
bcache_info=$(nm -l "$KERNEL_FILE" | grep " bcache$")
if [ -n "$bcache_info" ]; then
    echo "$ nm -l kernel/kernel | grep bcache"
    echo "$bcache_info"
    echo ""
    
    # Extract address
    bcache_addr=$(echo "$bcache_info" | awk '{print $1}')
    echo "$ addr2line -e kernel/kernel -f -p 0x$bcache_addr"
    addr2line -e "$KERNEL_FILE" -f -p "0x$bcache_addr" 2>/dev/null || echo "addr2line failed"
    echo ""
else
    echo "❌ bcache symbol not found"
fi

echo "🔍 DEMONSTRATION 2: Advanced Python Analysis"
echo "--------------------------------------------"
echo "Running enhanced DWARF extractor (basic mode):"
echo ""

# Run the Python script
if [ -f "scripts/dwarf-extractor.py" ]; then
    echo "$ python3 scripts/dwarf-extractor.py $KERNEL_FILE --comprehensive"
    python3 scripts/dwarf-extractor.py "$KERNEL_FILE" --comprehensive
    echo ""
else
    echo "❌ dwarf-extractor.py not found"
fi

echo "🔍 DEMONSTRATION 3: Bash Script Analysis"
echo "----------------------------------------"
echo "Running address-to-source analyzer:"
echo ""

# Run the bash script
if [ -f "scripts/addr2line-analyzer.sh" ]; then
    echo "$ ./scripts/addr2line-analyzer.sh --bss --source-map"
    ./scripts/addr2line-analyzer.sh --bss --source-map
    echo ""
else
    echo "❌ addr2line-analyzer.sh not found"
fi

echo "🔍 DEMONSTRATION 4: Manual Command Examples"
echo "-------------------------------------------"
echo ""

echo "Example 1: List all BSS symbols with source locations:"
echo "$ nm -l kernel/kernel | grep ' B '"
nm -l "$KERNEL_FILE" | grep ' B ' | head -5
echo "... (showing first 5 results)"
echo ""

echo "Example 2: Get symbol sizes:"
echo "$ nm -S kernel/kernel | grep ' B ' | head -3"
nm -S "$KERNEL_FILE" | grep ' B ' | head -3
echo ""

echo "Example 3: Find function addresses:"
echo "$ nm kernel/kernel | grep ' T ' | head -5"
nm "$KERNEL_FILE" | grep ' T ' | head -5
echo ""

echo "🔍 DEMONSTRATION 5: Practical Use Cases"
echo "---------------------------------------"
echo ""

echo "Use Case 1: Finding memory layout for debugging"
echo "If you have a kernel panic at address 0x80001234:"
echo "1. Find which symbol contains this address"
echo "2. Map to source code location"
echo "3. Examine surrounding code for issues"
echo ""

echo "Use Case 2: Optimizing memory usage"
echo "1. Identify largest BSS variables"
echo "2. Locate their source code declarations"  
echo "3. Consider if they can be reduced or optimized"
echo ""

echo "Use Case 3: Understanding kernel structure"
echo "1. Map all global variables to source files"
echo "2. Analyze which modules use most memory"
echo "3. Understand relationships between components"
echo ""

echo "📁 Output Files Generated"
echo "------------------------"
if [ -d "analysis-output" ]; then
    echo "Files in analysis-output/:"
    ls -la analysis-output/ 2>/dev/null | grep -v '^d' | awk '{print "  📄 " $9 " (" $5 " bytes)"}' || echo "  (directory empty or does not exist)"
else
    echo "  (no analysis-output directory found)"
fi
echo ""

if [ -f "comprehensive-memory-analysis.txt" ]; then
    echo "📄 comprehensive-memory-analysis.txt - Main analysis report"
fi

if [ -f "comprehensive-memory-analysis.txt.json" ]; then
    echo "📄 comprehensive-memory-analysis.txt.json - Machine-readable data"
fi

echo ""
echo "✅ Demonstration complete!"
echo ""
echo "💡 Next Steps:"
echo "   1. Examine the generated analysis files"
echo "   2. Try the scripts with different options:"
echo "      - python3 scripts/dwarf-extractor.py kernel/kernel --all"
echo "      - ./scripts/addr2line-analyzer.sh --all"
echo "   3. Use addr2line manually to investigate specific addresses"
echo "   4. Use the analysis data to optimize kernel memory usage"