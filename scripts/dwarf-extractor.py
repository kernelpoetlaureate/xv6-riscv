#!/usr/bin/env python3
"""
Advanced DWARF Debug Information Extractor for XV6-RISCV
Maps every kernel address to exact source file and line number using DWARF debug information.
Implements comprehensive memory analysis techniques from research on addr2line, nm, objdump, and DWARF.
"""

import subprocess
import re
import sys
import json
import argparse
from collections import defaultdict, OrderedDict

class AdvancedDwarfExtractor:
    def __init__(self, kernel_file):
        self.kernel_file = kernel_file
        self.symbol_map = {}
        self.address_to_source = {}
        self.bss_globals = []
        self.memory_layout = {}
        
    def get_symbol_table_with_sources(self):
        """Extract symbols with nm -l to get source file:line annotations"""
        try:
            result = subprocess.run(['nm', '-l', self.kernel_file], 
                                  capture_output=True, text=True, check=True)
            
            symbols_with_sources = []
            for line in result.stdout.split('\n'):
                if not line.strip():
                    continue
                
                # Parse nm -l output: address type name [file:line]
                parts = line.split()
                if len(parts) >= 3:
                    address = parts[0]
                    symbol_type = parts[1]
                    name = parts[2]
                    source_info = parts[3] if len(parts) > 3 else "unknown"
                    
                    symbol_data = {
                        'address': f"0x{address}",
                        'type': symbol_type,
                        'name': name,
                        'source': source_info
                    }
                    
                    symbols_with_sources.append(symbol_data)
                    self.symbol_map[name] = symbol_data
                    
                    # Track BSS globals specifically
                    if symbol_type in ['B', 'b']:
                        self.bss_globals.append(symbol_data)
            
            return symbols_with_sources
            
        except subprocess.CalledProcessError as e:
            print(f"Error running nm -l: {e}")
            return []

    def get_enhanced_symbol_info(self):
        """Get symbols with sizes using nm -S"""
        try:
            result = subprocess.run(['nm', '-S', self.kernel_file], 
                                  capture_output=True, text=True, check=True)
            
            symbol_sizes = {}
            for line in result.stdout.split('\n'):
                if not line.strip():
                    continue
                
                parts = line.split()
                if len(parts) >= 4:
                    address = parts[0]
                    size_hex = parts[1]
                    symbol_type = parts[2]
                    name = parts[3]
                    
                    try:
                        size_bytes = int(size_hex, 16)
                        symbol_sizes[name] = {
                            'address': f"0x{address}",
                            'size': size_bytes,
                            'type': symbol_type,
                            'name': name
                        }
                    except ValueError:
                        continue
            
            return symbol_sizes
            
        except subprocess.CalledProcessError as e:
            print(f"Error running nm -S: {e}")
            return {}

    def get_address_to_source_mapping(self, addresses):
        """Map addresses to source locations using addr2line"""
        if not addresses:
            return {}
        
        try:
            # Run addr2line with -f -p -i flags for enhanced output
            cmd = ['addr2line', '-e', self.kernel_file, '-f', '-p', '-i'] + addresses
            result = subprocess.run(cmd, capture_output=True, text=True, check=True)
            
            mappings = {}
            lines = result.stdout.strip().split('\n')
            
            for i, addr in enumerate(addresses):
                if i < len(lines):
                    source_info = lines[i].strip()
                    mappings[addr] = source_info
                    self.address_to_source[addr] = source_info
            
            return mappings
            
        except subprocess.CalledProcessError as e:
            print(f"Error running addr2line: {e}")
            return {}

    def extract_dwarf_line_mapping(self):
        """Extract complete DWARF line number program using objdump --dwarf=decodedline"""
        try:
            result = subprocess.run(['objdump', '--dwarf=decodedline', self.kernel_file],
                                  capture_output=True, text=True, check=True)
            
            line_mappings = []
            current_file = None
            
            for line in result.stdout.split('\n'):
                line = line.strip()
                
                # Parse DWARF line mapping format
                # File name            Line number   Starting address
                if 'kernel/' in line and '.c' in line:
                    current_file = line
                elif current_file and re.match(r'^0x[0-9a-f]+', line):
                    parts = line.split()
                    if len(parts) >= 2:
                        address = parts[0]
                        line_num = parts[1] if len(parts) > 1 else "0"
                        
                        line_mappings.append({
                            'address': address,
                            'file': current_file,
                            'line': line_num
                        })
            
            return line_mappings
            
        except subprocess.CalledProcessError as e:
            print(f"Error extracting DWARF line mapping: {e}")
            return []

    def generate_source_interleaved_disassembly(self, output_file):
        """Generate disassembly with source code using objdump -S -l"""
        try:
            result = subprocess.run(['objdump', '-S', '-l', self.kernel_file],
                                  capture_output=True, text=True, check=True)
            
            with open(output_file, 'w') as f:
                f.write("# XV6-RISCV Kernel Disassembly with Source Code\n")
                f.write("# Generated using objdump -S -l\n")
                f.write("# Maps assembly instructions to exact source file:line\n\n")
                f.write(result.stdout)
            
            print(f"Source-interleaved disassembly written to: {output_file}")
            
        except subprocess.CalledProcessError as e:
            print(f"Error generating source-interleaved disassembly: {e}")

    def map_bss_symbols_to_source(self):
        """Create detailed BSS symbol to source code mapping"""
        symbols_with_sources = self.get_symbol_table_with_sources()
        symbol_sizes = self.get_enhanced_symbol_info()
        
        bss_mappings = []
        bss_addresses = []
        
        for symbol in symbols_with_sources:
            if symbol['type'] in ['B', 'b']:
                bss_addresses.append(symbol['address'])
        
        # Get detailed source information for BSS addresses
        source_mappings = self.get_address_to_source_mapping(bss_addresses)
        
        for symbol in symbols_with_sources:
            if symbol['type'] in ['B', 'b']:
                addr = symbol['address']
                name = symbol['name']
                
                # Get size if available
                size_info = symbol_sizes.get(name, {})
                size_bytes = size_info.get('size', 0)
                
                # Get enhanced source location
                source_detail = source_mappings.get(addr, symbol['source'])
                
                bss_mapping = {
                    'name': name,
                    'address': addr,
                    'size': size_bytes,
                    'source_file': symbol['source'].split(':')[0] if ':' in symbol['source'] else symbol['source'],
                    'source_line': symbol['source'].split(':')[1] if ':' in symbol['source'] else 'unknown',
                    'source_detail': source_detail,
                    'section': '.bss'
                }
                
                bss_mappings.append(bss_mapping)
        
        return sorted(bss_mappings, key=lambda x: int(x['address'], 16))

    def extract_dwarf_info(self, output_file):
        """Extract and organize comprehensive DWARF debug information"""
        
        try:
            # Get DWARF info
            result = subprocess.run(['objdump', '-W', self.kernel_file], 
                                  capture_output=True, text=True, check=True)
            
            with open(output_file, 'w') as f:
                f.write("# XV6-RISCV Advanced DWARF Debug Information Analysis\n")
                f.write(f"# Extracted from: {self.kernel_file}\n")
                f.write("# Maps every kernel address to exact source file and line number\n\n")
                
                lines = result.stdout.split('\n')
                current_section = None
                
                for line in lines:
                    line = line.strip()
                    
                    # Detect section headers
                    if line.startswith('Contents of the '):
                        current_section = line
                        f.write(f"\n=== {current_section} ===\n")
                        continue
                    
                    # Process different sections with enhanced filtering
                    if current_section:
                        if '.debug_info' in current_section:
                            # Extract compilation units and DIEs
                            if any(tag in line for tag in ['DW_TAG_', 'DW_AT_name', 'DW_AT_decl_file', 'DW_AT_decl_line']):
                                f.write(f"  {line}\n")
                        elif '.debug_line' in current_section:
                            # Extract line number information
                            if any(keyword in line for keyword in ['File name', 'Line Number', 'Address', 'kernel/']):
                                f.write(f"  {line}\n")
                        elif '.debug_abbrev' in current_section:
                            # Extract abbreviation tables
                            if line and not line.startswith('Contents'):
                                f.write(f"  {line}\n")
                        else:
                            # Other debug sections
                            if line and not line.startswith('Contents'):
                                f.write(f"  {line}\n")
            
            print(f"Advanced DWARF debug information extracted to: {output_file}")
            
        except subprocess.CalledProcessError as e:
            print(f"Error extracting DWARF info: {e}")
            sys.exit(1)

    def generate_comprehensive_memory_analysis(self, output_file):
        """Generate complete memory layout with source attribution"""
        
        # Get all symbol and source information
        symbols_with_sources = self.get_symbol_table_with_sources()
        symbol_sizes = self.get_enhanced_symbol_info()
        bss_mappings = self.map_bss_symbols_to_source()
        
        # Generate comprehensive analysis
        analysis = {
            'kernel_file': self.kernel_file,
            'generation_time': subprocess.run(['date'], capture_output=True, text=True).stdout.strip(),
            'memory_layout': {},
            'bss_globals': bss_mappings,
            'all_symbols': symbols_with_sources,
            'symbol_statistics': {}
        }
        
        # Calculate statistics
        symbol_counts = defaultdict(int)
        total_bss_size = 0
        
        for symbol in symbols_with_sources:
            symbol_counts[symbol['type']] += 1
            if symbol['type'] in ['B', 'b']:
                size_info = symbol_sizes.get(symbol['name'], {})
                total_bss_size += size_info.get('size', 0)
        
        analysis['symbol_statistics'] = {
            'total_symbols': len(symbols_with_sources),
            'symbol_type_counts': dict(symbol_counts),
            'total_bss_size': total_bss_size,
            'bss_variable_count': len(bss_mappings)
        }
        
        # Write JSON output
        with open(f"{output_file}.json", 'w') as f:
            json.dump(analysis, f, indent=2)
        
        # Write human-readable report
        with open(output_file, 'w') as f:
            f.write("# XV6-RISCV Comprehensive Memory Analysis Report\n")
            f.write("# Maps every kernel address to exact source file and line number\n")
            f.write(f"# Generated from: {self.kernel_file}\n\n")
            
            # Memory layout visualization
            f.write("=== MEMORY LAYOUT WITH SOURCE ATTRIBUTION ===\n")
            f.write("Address            Size    Symbol           Source Location\n")
            f.write("-" * 72 + "\n")
            
            # Sort all symbols by address for memory layout view
            all_symbols_sorted = sorted(symbols_with_sources, key=lambda x: int(x['address'], 16))
            
            for symbol in all_symbols_sorted:
                addr = symbol['address']
                name = symbol['name'][:20]  # Truncate long names
                source = symbol['source']
                
                # Get size information
                size_info = symbol_sizes.get(symbol['name'], {})
                size_bytes = size_info.get('size', 0)
                size_str = f"{size_bytes:6} B" if size_bytes > 0 else "   ? B"
                
                f.write(f"{addr:16} {size_str:8} {name:16} {source}\n")
            
            # BSS Section Analysis
            f.write(f"\n=== BSS SECTION DETAILED ANALYSIS ===\n")
            f.write(f"Total BSS variables: {len(bss_mappings)}\n")
            f.write(f"Total BSS size: {total_bss_size:,} bytes ({total_bss_size/1024:.1f} KB)\n\n")
            
            f.write("BSS Variable Details:\n")
            f.write("Name                           Address          Size      Source Location\n")
            f.write("-" * 80 + "\n")
            
            for bss_var in bss_mappings:
                name = bss_var['name'][:30]
                addr = bss_var['address']
                size = bss_var['size']
                size_str = f"{size:6} B" if size > 0 else "   ? B"
                source = f"{bss_var['source_file']}:{bss_var['source_line']}"
                
                f.write(f"{name:30} {addr:16} {size_str:9} {source}\n")
            
            # Symbol Statistics
            f.write(f"\n=== SYMBOL STATISTICS ===\n")
            f.write(f"Total symbols: {analysis['symbol_statistics']['total_symbols']}\n")
            f.write(f"BSS variables: {analysis['symbol_statistics']['bss_variable_count']}\n")
            f.write(f"Total BSS size: {total_bss_size:,} bytes\n\n")
            
            f.write("Symbol type breakdown:\n")
            for sym_type, count in sorted(analysis['symbol_statistics']['symbol_type_counts'].items()):
                type_desc = {
                    'T': 'Text (global function)',
                    't': 'Text (local function)', 
                    'B': 'BSS (global uninitialized)',
                    'b': 'BSS (local uninitialized)',
                    'D': 'Data (global initialized)',
                    'd': 'Data (local initialized)',
                    'R': 'Read-only data',
                    'r': 'Read-only data (local)'
                }.get(sym_type, 'Other')
                
                f.write(f"  {sym_type}: {count:4} ({type_desc})\n")
        
        print(f"Comprehensive memory analysis written to: {output_file}")
        print(f"JSON data written to: {output_file}.json")

    def extract_source_lines(self, output_file):
        """Extract enhanced source line mapping information"""
        
        try:
            result = subprocess.run(['objdump', '-l', self.kernel_file],
                                  capture_output=True, text=True, check=True)
            
            with open(output_file, 'w') as f:
                f.write("# XV6-RISCV Enhanced Source Line Mapping\n")
                f.write(f"# Extracted from: {self.kernel_file}\n")
                f.write("# Maps assembly instructions to source code locations\n\n")
                
                current_file = None
                for line in result.stdout.split('\n'):
                    # Detect source file references
                    if line.endswith('.c') or line.endswith('.S'):
                        current_file = line.strip()
                        f.write(f"\n=== {current_file} ===\n")
                    elif line.strip() and current_file:
                        # Extract address and instruction mappings with enhanced filtering
                        if ':' in line and any(c.isdigit() for c in line) and len(line) > 10:
                            f.write(f"  {line.strip()}\n")
                            
            print(f"Enhanced source line mapping extracted to: {output_file}")
            
        except subprocess.CalledProcessError as e:
            print(f"Error extracting source line mapping: {e}")

def main():
    parser = argparse.ArgumentParser(
        description='Advanced DWARF Debug Information Extractor for XV6-RISCV',
        epilog='''
Examples:
  %(prog)s kernel/kernel                    # Extract basic DWARF info
  %(prog)s kernel/kernel --all              # Generate all analysis files
  %(prog)s kernel/kernel --bss-analysis    # Focus on BSS section analysis
  %(prog)s kernel/kernel --source-map      # Generate source-interleaved disassembly
        ''',
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    
    parser.add_argument('kernel', help='Path to kernel binary')
    parser.add_argument('--all', action='store_true',
                       help='Generate all analysis files')
    parser.add_argument('--bss-analysis', action='store_true',
                       help='Generate BSS section analysis')
    parser.add_argument('--source-map', action='store_true',
                       help='Generate source-interleaved disassembly')
    parser.add_argument('--dwarf-info', action='store_true',
                       help='Extract DWARF debug information')
    parser.add_argument('--comprehensive', action='store_true',
                       help='Generate comprehensive memory analysis report')
    
    args = parser.parse_args()
    
    if not any([args.all, args.bss_analysis, args.source_map, args.dwarf_info, args.comprehensive]):
        # Default behavior - extract basic DWARF info
        args.dwarf_info = True
    
    extractor = AdvancedDwarfExtractor(args.kernel)
    
    if args.all or args.dwarf_info:
        extractor.extract_dwarf_info("kernel-dwarf-info.txt")
    
    if args.all or args.source_map:
        extractor.extract_source_lines("kernel-source-lines.txt")
        extractor.generate_source_interleaved_disassembly("kernel-source-disassembly.txt")
    
    if args.all or args.bss_analysis:
        bss_mappings = extractor.map_bss_symbols_to_source()
        with open("bss-analysis.json", 'w') as f:
            json.dump(bss_mappings, f, indent=2)
        print(f"BSS analysis written to: bss-analysis.json")
    
    if args.all or args.comprehensive:
        extractor.generate_comprehensive_memory_analysis("comprehensive-memory-analysis.txt")

if __name__ == '__main__':
    main()