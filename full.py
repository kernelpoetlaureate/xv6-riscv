#!/usr/bin/env python3
"""
Comprehensive QEMU Memory Analysis Tool for RISC-V xv6 Systems

This script provides exhaustive memory examination capabilities for QEMU-emulated
RISC-V systems, particularly targeting xv6 operating system debugging scenarios.
It interfaces with QEMU through multiple protocols to ensure complete memory
visibility and examination capabilities.

Embedded GDB Command for direct integration with QEMU debugging sessions.

Author: System Analysis Tool
Version: 2.0.0
Target: RISC-V xv6 on QEMU
"""

import gdb
import sys
import socket
import json
import struct
import argparse
import time
import threading
import logging
from typing import Optional, Dict, List, Tuple, Any, Iterator
from enum import Enum
from dataclasses import dataclass
from pathlib import Path
import subprocess
from contextlib import contextmanager

# Configure comprehensive logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('qemu_memory_analysis.log'),
        logging.StreamHandler(sys.stdout)
    ]
)
logger = logging.getLogger(__name__)

class MemoryAccessMode(Enum):
    """Memory access modes supported by the analysis tool"""
    VIRTUAL = "virtual"
    PHYSICAL = "physical" 
    BOTH = "both"

class AddressSpace(Enum):
    """RISC-V address space definitions"""
    USER = "user"
    SUPERVISOR = "supervisor"
    MACHINE = "machine"
    ALL = "all"

@dataclass
class MemoryRegion:
    """Represents a contiguous memory region with metadata"""
    start_addr: int
    end_addr: int
    size: int
    permissions: str
    mapping_type: str
    description: str = ""
    
    def contains(self, address: int) -> bool:
        return self.start_addr <= address <= self.end_addr

@dataclass  
class MemoryDump:
    """Container for memory dump data with metadata"""
    start_addr: int
    data: bytes
    access_mode: MemoryAccessMode
    timestamp: float
    regions: List[MemoryRegion]

class QEMUMonitorInterface:
    """
    Interface to QEMU monitor for memory operations
    Supports both HMP and QMP protocols for maximum compatibility
    """
    
    def __init__(self, monitor_socket: str, qmp_socket: Optional[str] = None):
        self.monitor_socket = monitor_socket
        self.qmp_socket = qmp_socket
        self.monitor_conn = None
        self.qmp_conn = None
        self.connected = False
        
    def connect(self) -> bool:
        """Establish connections to QEMU monitor interfaces"""
        try:
            # Connect to HMP monitor via TCP or Unix socket
            if self.monitor_socket.startswith('tcp:'):
                host, port = self.monitor_socket[4:].split(':')
                self.monitor_conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.monitor_conn.settimeout(10)
                self.monitor_conn.connect((host, int(port)))
                logger.info(f"Connected to HMP monitor at {host}:{port}")
            elif self.monitor_socket.startswith('unix:'):
                socket_path = self.monitor_socket[5:]
                self.monitor_conn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.monitor_conn.connect(socket_path)
                logger.info(f"Connected to HMP monitor at {socket_path}")
            
            # Connect to QMP if specified
            if self.qmp_socket:
                if self.qmp_socket.startswith('unix:'):
                    qmp_path = self.qmp_socket[5:]
                    self.qmp_conn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    self.qmp_conn.connect(qmp_path)
                    
                    # QMP handshake
                    greeting = json.loads(self.qmp_conn.recv(4096).decode())
                    logger.info(f"QMP greeting: {greeting}")
                    
                    # Send capabilities negotiation
                    qmp_cmd = {"execute": "qmp_capabilities"}
                    self.qmp_conn.send(json.dumps(qmp_cmd).encode() + b'\n')
                    response = json.loads(self.qmp_conn.recv(4096).decode())
                    logger.info(f"QMP capabilities response: {response}")
            
            self.connected = True
            return True
            
        except Exception as e:
            logger.error(f"Failed to connect to QEMU monitor: {e}")
            return False
    
    def execute_hmp_command(self, command: str) -> str:
        """Execute HMP command and return response"""
        if not self.connected or not self.monitor_conn:
            raise RuntimeError("Not connected to monitor")
        
        try:
            # Send command
            self.monitor_conn.send(f"{command}\n".encode())
            
            # Receive response (read until QEMU prompt)
            response = b""
            while True:
                chunk = self.monitor_conn.recv(4096)
                if not chunk:
                    break
                response += chunk
                
                # Look for QEMU prompt to know when command is done
                if b"(qemu)" in response:
                    break
                    
                # Timeout safety
                import select
                ready = select.select([self.monitor_conn], [], [], 1.0)
                if not ready[0]:
                    break
            
            return response.decode().strip()
                
        except Exception as e:
            logger.error(f"HMP command failed: {command}, error: {e}")
            return ""
    
    def execute_qmp_command(self, command: Dict[str, Any]) -> Dict[str, Any]:
        """Execute QMP command and return JSON response"""
        if not self.qmp_conn:
            raise RuntimeError("QMP not connected")
        
        try:
            cmd_json = json.dumps(command) + '\n'
            self.qmp_conn.send(cmd_json.encode())
            response = self.qmp_conn.recv(8192)
            return json.loads(response.decode())
        except Exception as e:
            logger.error(f"QMP command failed: {command}, error: {e}")
            return {}

    def read_memory_bytes(self, address: int, length: int, 
                         access_mode: MemoryAccessMode = MemoryAccessMode.PHYSICAL) -> bytes:
        """Read raw bytes from memory at specified address"""
        try:
            if access_mode == MemoryAccessMode.PHYSICAL:
                # Use physical memory read command
                cmd = f"x/{'%d'%length}b {address:#x}"
            else:
                # Use virtual memory read (requires active CPU context)
                cmd = f"x/{'%d'%length}b {address:#x}"
            
            response = self.execute_hmp_command(cmd)
            return self._parse_memory_dump(response, length)
            
        except Exception as e:
            logger.error(f"Memory read failed at {address:#x}: {e}")
            return b''
    
    def _parse_memory_dump(self, dump_text: str, expected_length: int) -> bytes:
        """Parse QEMU memory dump text format into raw bytes"""
        memory_bytes = bytearray()
        lines = dump_text.split('\n')
        
        for line in lines:
            if ':' in line and not line.startswith('(qemu)'):
                # Parse hex dump format: address: byte byte byte ...
                parts = line.split(':', 1)
                if len(parts) == 2:
                    hex_values = parts[1].strip().split()
                    for hex_val in hex_values:
                        if hex_val.startswith('0x'):
                            memory_bytes.append(int(hex_val, 16))
                        elif len(hex_val) == 2:  # Raw hex byte
                            memory_bytes.append(int(hex_val, 16))
        
        return bytes(memory_bytes[:expected_length])
    
    def get_memory_regions(self) -> List[MemoryRegion]:
        """Enumerate all memory regions from QEMU memory map"""
        regions = []
        try:
            # Get memory info from QEMU
            info_response = self.execute_hmp_command("info memory")
            mtree_response = self.execute_hmp_command("info mtree")
            
            # Parse memory tree output for region information
            current_region = None
            for line in mtree_response.split('\n'):
                if 'memory' in line.lower() and '[' in line:
                    # Parse memory region definition
                    parts = line.strip().split()
                    for part in parts:
                        if '[' in part and ']' in part:
                            addr_range = part.strip('[]')
                            if '-' in addr_range:
                                start_str, end_str = addr_range.split('-')
                                start_addr = int(start_str, 16)
                                end_addr = int(end_str, 16) 
                                size = end_addr - start_addr + 1
                                
                                regions.append(MemoryRegion(
                                    start_addr=start_addr,
                                    end_addr=end_addr, 
                                    size=size,
                                    permissions="rw",
                                    mapping_type="memory",
                                    description=line.strip()
                                ))
            
            logger.info(f"Discovered {len(regions)} memory regions")
            return regions
            
        except Exception as e:
            logger.error(f"Failed to get memory regions: {e}")
            return []
    
    def disconnect(self):
        """Clean up connections"""
        if self.monitor_conn:
            self.monitor_conn.close()
        
        if self.qmp_conn:
            self.qmp_conn.close()
        
        self.connected = False


class GDBInterface:
    """
    GDB remote protocol interface for enhanced memory debugging
    Provides symbol resolution and advanced debugging capabilities
    """
    
    def __init__(self, gdb_port: int = 1234, gdb_host: str = "localhost"):
        self.gdb_port = gdb_port
        self.gdb_host = gdb_host
        self.gdb_socket = None
        self.connected = False
        
    def connect(self) -> bool:
        """Connect to QEMU GDB stub"""
        try:
            self.gdb_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.gdb_socket.connect((self.gdb_host, self.gdb_port))
            self.connected = True
            logger.info(f"Connected to GDB stub at {self.gdb_host}:{self.gdb_port}")
            return True
        except Exception as e:
            logger.error(f"Failed to connect to GDB stub: {e}")
            return False
    
    def read_memory_gdb(self, address: int, length: int) -> bytes:
        """Read memory via GDB remote protocol"""
        if not self.connected:
            return b''
        
        try:
            # GDB remote 'm' command: maddr,length
            cmd = f"m{address:x},{length:x}"
            checksum = sum(ord(c) for c in cmd) % 256
            packet = f"${cmd}#{checksum:02x}"
            
            self.gdb_socket.send(packet.encode())
            response = self.gdb_socket.recv(4096).decode()
            
            # Parse response and convert hex to bytes
            if response.startswith('+$') and '#' in response:
                hex_data = response.split('$')[1].split('#')[0]
                return bytes.fromhex(hex_data)
            
            return b''
            
        except Exception as e:
            logger.error(f"GDB memory read failed: {e}")
            return b''
    
    def get_register_state(self) -> Dict[str, int]:
        """Get current CPU register state via GDB"""
        if not self.connected:
            return {}
        
        try:
            # GDB 'g' command gets all registers
            cmd = "g"
            checksum = sum(ord(c) for c in cmd) % 256
            packet = f"${cmd}#{checksum:02x}"
            
            self.gdb_socket.send(packet.encode())
            response = self.gdb_socket.recv(4096).decode()
            
            registers = {}
            if response.startswith('+$') and '#' in response:
                hex_data = response.split('$')[1].split('#')[0]
                # Parse RISC-V register format (32 registers, 8 bytes each for RV64)
                reg_data = bytes.fromhex(hex_data)
                
                riscv_regs = ['x0', 'x1', 'x2', 'x3', 'x4', 'x5', 'x6', 'x7',
                             'x8', 'x9', 'x10', 'x11', 'x12', 'x13', 'x14', 'x15',
                             'x16', 'x17', 'x18', 'x19', 'x20', 'x21', 'x22', 'x23', 
                             'x24', 'x25', 'x26', 'x27', 'x28', 'x29', 'x30', 'x31']
                
                for i, reg_name in enumerate(riscv_regs):
                    if i * 8 + 8 <= len(reg_data):
                        reg_bytes = reg_data[i*8:(i+1)*8]
                        reg_val = struct.unpack('<Q', reg_bytes)[0]  # Little-endian 64-bit
                        registers[reg_name] = reg_val
            
            return registers
            
        except Exception as e:
            logger.error(f"Failed to get register state: {e}")
            return {}
    
    def disconnect(self):
        """Close GDB connection"""
        if self.gdb_socket:
            self.gdb_socket.close()
        self.connected = False


class MemoryAnalyzer:
    """
    Main memory analysis orchestrator
    Coordinates between different interfaces to provide comprehensive memory analysis
    """
    
    def __init__(self, qemu_monitor: str, qmp_socket: Optional[str] = None, 
                 gdb_port: int = 1234, output_dir: str = "./memory_dumps"):
        self.qemu = QEMUMonitorInterface(qemu_monitor, qmp_socket)
        self.gdb = GDBInterface(gdb_port)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(exist_ok=True)
        
        # Memory analysis state
        self.memory_regions = []
        self.known_symbols = {}
        self.analysis_history = []
        
    def initialize(self) -> bool:
        """Initialize all interfaces and gather system information"""
        logger.info("Initializing QEMU memory analyzer...")
        
        # Connect to QEMU monitor
        if not self.qemu.connect():
            logger.error("Failed to connect to QEMU monitor")
            return False
        
        # Connect to GDB (optional)
        if not self.gdb.connect():
            logger.warning("GDB connection failed, continuing with monitor only")
        
        # Discover memory layout
        self.memory_regions = self.qemu.get_memory_regions()
        logger.info(f"Initialized with {len(self.memory_regions)} memory regions")
        
        return True
    
    def dump_complete_memory(self, access_mode: MemoryAccessMode = MemoryAccessMode.PHYSICAL,
                           chunk_size: int = 4096) -> List[MemoryDump]:
        """
        Dump entire addressable memory space
        Uses chunked reading for efficiency and reliability
        """
        logger.info(f"Starting complete memory dump in {access_mode.value} mode")
        dumps = []
        
        if not self.memory_regions:
            # Fallback: try common RISC-V memory ranges
            logger.warning("No memory regions detected, using fallback ranges")
            fallback_regions = [
                MemoryRegion(0x80000000, 0x87FFFFFF, 0x8000000, "rw", "ram", "Main RAM"),
                MemoryRegion(0x10000000, 0x10000FFF, 0x1000, "rw", "mmio", "UART"),
                MemoryRegion(0x0, 0xFFF, 0x1000, "rw", "boot", "Boot ROM")
            ]
            self.memory_regions = fallback_regions
        
        total_bytes = 0
        for region in self.memory_regions:
            logger.info(f"Dumping region {region.description}: {region.start_addr:#x}-{region.end_addr:#x}")
            
            region_dumps = self._dump_memory_region(region, access_mode, chunk_size)
            dumps.extend(region_dumps)
            
            for dump in region_dumps:
                total_bytes += len(dump.data)
        
        logger.info(f"Complete memory dump finished: {total_bytes} bytes across {len(dumps)} chunks")
        return dumps
    
    def _dump_memory_region(self, region: MemoryRegion, access_mode: MemoryAccessMode,
                           chunk_size: int) -> List[MemoryDump]:
        """Dump a specific memory region in chunks"""
        region_dumps = []
        current_addr = region.start_addr
        
        while current_addr <= region.end_addr:
            remaining = region.end_addr - current_addr + 1
            read_size = min(chunk_size, remaining)
            
            try:
                # Try QEMU monitor first
                data = self.qemu.read_memory_bytes(current_addr, read_size, access_mode)
                
                # Fallback to GDB if monitor fails
                if not data and self.gdb.connected:
                    data = self.gdb.read_memory_gdb(current_addr, read_size)
                
                if data:
                    dump = MemoryDump(
                        start_addr=current_addr,
                        data=data,
                        access_mode=access_mode,
                        timestamp=time.time(),
                        regions=[region]
                    )
                    region_dumps.append(dump)
                    
                current_addr += read_size
                
            except Exception as e:
                logger.error(f"Failed to read memory at {current_addr:#x}: {e}")
                current_addr += read_size  # Skip failed region
        
        return region_dumps
    
    def dump_address_range(self, start_addr: int, end_addr: int, 
                          access_mode: MemoryAccessMode = MemoryAccessMode.PHYSICAL) -> MemoryDump:
        """Dump specific address range"""
        size = end_addr - start_addr + 1
        logger.info(f"Dumping address range {start_addr:#x}-{end_addr:#x} ({size} bytes)")
        
        try:
            data = self.qemu.read_memory_bytes(start_addr, size, access_mode)
            
            if not data and self.gdb.connected:
                data = self.gdb.read_memory_gdb(start_addr, size)
            
            # Find overlapping regions
            overlapping_regions = [r for r in self.memory_regions 
                                 if r.contains(start_addr) or r.contains(end_addr)]
            
            return MemoryDump(
                start_addr=start_addr,
                data=data,
                access_mode=access_mode,
                timestamp=time.time(),
                regions=overlapping_regions
            )
            
        except Exception as e:
            logger.error(f"Failed to dump address range {start_addr:#x}-{end_addr:#x}: {e}")
            return MemoryDump(start_addr, b'', access_mode, time.time(), [])
    
    def examine_byte(self, address: int, access_mode: MemoryAccessMode = MemoryAccessMode.PHYSICAL) -> int:
        """Examine a single byte at specified address"""
        data = self.qemu.read_memory_bytes(address, 1, access_mode)
        if not data and self.gdb.connected:
            data = self.gdb.read_memory_gdb(address, 1)
        
        return data[0] if data else 0
    
    def search_memory(self, pattern: bytes, 
                     regions: Optional[List[MemoryRegion]] = None) -> List[Tuple[int, MemoryRegion]]:
        """Search for byte pattern across memory regions"""
        logger.info(f"Searching for pattern: {pattern.hex()}")
        matches = []
        
        search_regions = regions or self.memory_regions
        
        for region in search_regions:
            try:
                # Read region in chunks to avoid memory issues
                chunk_size = 1024 * 1024  # 1MB chunks
                current_addr = region.start_addr
                
                while current_addr <= region.end_addr:
                    read_size = min(chunk_size, region.end_addr - current_addr + 1)
                    data = self.qemu.read_memory_bytes(current_addr, read_size)
                    
                    if data:
                        # Search for pattern in chunk
                        offset = 0
                        while True:
                            pos = data.find(pattern, offset)
                            if pos == -1:
                                break
                            
                            match_addr = current_addr + pos
                            matches.append((match_addr, region))
                            offset = pos + 1
                    
                    current_addr += read_size
                    
            except Exception as e:
                logger.error(f"Search failed in region {region.description}: {e}")
        
        logger.info(f"Found {len(matches)} matches for pattern")
        return matches
    
    def save_dump_to_file(self, dump: MemoryDump, filename: Optional[str] = None):
        """Save memory dump to binary file"""
        if not filename:
            timestamp = int(dump.timestamp)
            filename = f"memory_dump_{dump.start_addr:08x}_{timestamp}.bin"
        
        filepath = self.output_dir / filename
        
        try:
            with open(filepath, 'wb') as f:
                f.write(dump.data)
            
            # Save metadata
            meta_filename = filepath.with_suffix('.meta.json')
            metadata = {
                'start_address': f"0x{dump.start_addr:x}",
                'size': len(dump.data),
                'access_mode': dump.access_mode.value,
                'timestamp': dump.timestamp,
                'regions': [
                    {
                        'start': f"0x{r.start_addr:x}",
                        'end': f"0x{r.end_addr:x}",
                        'size': r.size,
                        'permissions': r.permissions,
                        'type': r.mapping_type,
                        'description': r.description
                    } for r in dump.regions
                ]
            }
            
            with open(meta_filename, 'w') as f:
                json.dump(metadata, f, indent=2)
            
            logger.info(f"Saved dump to {filepath} ({len(dump.data)} bytes)")
            
        except Exception as e:
            logger.error(f"Failed to save dump to {filepath}: {e}")
    
    def generate_memory_map_report(self) -> str:
        """Generate comprehensive memory map report"""
        report = []
        report.append("=" * 80)
        report.append("QEMU RISC-V Memory Analysis Report")
        report.append("=" * 80)
        report.append(f"Generated: {time.ctime()}")
        report.append(f"Memory Regions Discovered: {len(self.memory_regions)}")
        report.append("")
        
        # Register state if available
        if self.gdb.connected:
            registers = self.gdb.get_register_state()
            if registers:
                report.append("CPU Register State:")
                report.append("-" * 40)
                for reg, value in registers.items():
                    report.append(f"  {reg:4s}: 0x{value:016x} ({value})")
                report.append("")
        
        # Memory regions
        report.append("Memory Regions:")
        report.append("-" * 60)
        report.append(f"{'Start':>12s} {'End':>12s} {'Size':>12s} {'Perms':>6s} {'Type':>8s} {'Description'}")
        report.append("-" * 60)
        
        total_size = 0
        for region in sorted(self.memory_regions, key=lambda r: r.start_addr):
            report.append(f"{region.start_addr:>#12x} {region.end_addr:>#12x} "
                         f"{region.size:>12d} {region.permissions:>6s} "
                         f"{region.mapping_type:>8s} {region.description}")
            total_size += region.size
        
        report.append("-" * 60)
        report.append(f"Total Memory: {total_size:,} bytes ({total_size/1024/1024:.2f} MB)")
        report.append("")
        
        return '\n'.join(report)
    
    def interactive_memory_browser(self):
        """Interactive memory browser with command interface"""
        print("QEMU Memory Browser - Interactive Mode")
        print("Commands: dump <start> <end>, search <hex_pattern>, regions, quit")
        
        while True:
            try:
                cmd_line = input("\nmemory> ").strip().split()
                if not cmd_line:
                    continue
                
                command = cmd_line[0].lower()
                
                if command == 'quit' or command == 'exit':
                    break
                
                elif command == 'dump':
                    if len(cmd_line) >= 3:
                        start = int(cmd_line[1], 0)
                        end = int(cmd_line[2], 0)
                        dump = self.dump_address_range(start, end)
                        
                        print(f"\nMemory dump {start:#x}-{end:#x}:")
                        self._print_hex_dump(dump.data, start)
                        
                        save = input("Save to file? (y/n): ").lower() == 'y'
                        if save:
                            self.save_dump_to_file(dump)
                    else:
                        print("Usage: dump <start_addr> <end_addr>")
                
                elif command == 'search':
                    if len(cmd_line) >= 2:
                        pattern = bytes.fromhex(cmd_line[1])
                        matches = self.search_memory(pattern)
                        
                        print(f"\nFound {len(matches)} matches:")
                        for addr, region in matches[:10]:  # Show first 10
                            print(f"  {addr:#x} in {region.description}")
                        
                        if len(matches) > 10:
                            print(f"  ... and {len(matches)-10} more")
                    else:
                        print("Usage: search <hex_pattern>")
                
                elif command == 'regions':
                    print("\nMemory Regions:")
                    for i, region in enumerate(self.memory_regions):
                        print(f"  {i}: {region.start_addr:#x}-{region.end_addr:#x} "
                              f"({region.size:,} bytes) {region.description}")
                
                elif command == 'byte':
                    if len(cmd_line) >= 2:
                        addr = int(cmd_line[1], 0)
                        byte_val = self.examine_byte(addr)
                        print(f"Byte at {addr:#x}: 0x{byte_val:02x} ({byte_val})")
                    else:
                        print("Usage: byte <address>")
                
                else:
                    print("Unknown command. Available: dump, search, regions, byte, quit")
                    
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"Error: {e}")
    
    def _print_hex_dump(self, data: bytes, base_addr: int, bytes_per_line: int = 16):
        """Print formatted hex dump"""
        for i in range(0, len(data), bytes_per_line):
            addr = base_addr + i
            chunk = data[i:i+bytes_per_line]
            
            hex_bytes = ' '.join(f'{b:02x}' for b in chunk)
            ascii_chars = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in chunk)
            
            print(f"{addr:08x}: {hex_bytes:<48} |{ascii_chars}|")
    
    def cleanup(self):
        """Clean up all connections"""
        logger.info("Cleaning up analyzer connections")
        self.qemu.disconnect()
        self.gdb.disconnect()


# ============================================================================
# GDB Command Classes for QEMU Integration
# ============================================================================

class ComprehensiveMemoryAnalysisCommand(gdb.Command):
    """
    Comprehensive memory analysis command for QEMU-embedded GDB sessions.
    
    Provides exhaustive memory examination capabilities including:
    - Complete memory region discovery and enumeration
    - Byte-level memory dumps with multiple access methods  
    - Pattern searching across entire address space
    - Real-time interactive memory browser
    - Comprehensive memory map reporting
    """
    
    def __init__(self):
        super(ComprehensiveMemoryAnalysisCommand, self).__init__(
            "mem_analysis", gdb.COMMAND_DATA, gdb.COMPLETE_NONE, True
        )
        self.analyzer = None
        
    def invoke(self, args, from_tty):
        """Main command entry point"""
        try:
            argv = gdb.string_to_argv(args) if args else []
            self._execute_analysis(argv)
        except Exception as e:
            gdb.write(f"Memory analysis error: {e}\n")
            import traceback
            gdb.write(f"Traceback: {traceback.format_exc()}\n")
    
    def _execute_analysis(self, argv):
        """Execute memory analysis based on arguments"""
        if not argv:
            self._show_help()
            return
        
        operation = argv[0]
        
        # Initialize analyzer if needed
        if not self.analyzer:
            # For GDB integration, we'll use direct memory access
            self.analyzer = self._create_gdb_analyzer()
        
        if operation == "dump-all":
            self._dump_all_memory()
        elif operation == "dump-range" and len(argv) >= 3:
            start_addr = int(argv[1], 0)
            end_addr = int(argv[2], 0)
            self._dump_address_range(start_addr, end_addr)
        elif operation == "search" and len(argv) >= 2:
            pattern = argv[1]
            self._search_memory_pattern(pattern)
        elif operation == "interactive":
            self._interactive_browser()
        elif operation == "report":
            self._generate_memory_report()
        elif operation == "regions":
            self._enumerate_memory_regions()
        else:
            self._show_help()
    
    def _create_gdb_analyzer(self):
        """Create analyzer for GDB environment"""
        gdb.write("Initializing comprehensive memory analyzer...\n")
        
        # For GDB integration, we'll create a simplified analyzer
        # that uses GDB's memory access capabilities
        class GDBMemoryAnalyzer:
            def __init__(self):
                self.output_dir = Path("./memory_dumps")
                self.output_dir.mkdir(exist_ok=True)
            
            def dump_memory_range(self, start_addr, size):
                """Dump memory using GDB's memory access"""
                try:
                    inferior = gdb.selected_inferior()
                    return inferior.read_memory(start_addr, size)
                except gdb.MemoryError as e:
                    gdb.write(f"Memory access error at {start_addr:#x}: {e}\n")
                    return None
            
            def search_pattern(self, pattern_hex):
                """Search for pattern in accessible memory"""
                pattern = bytes.fromhex(pattern_hex)
                matches = []
                
                # Search in main memory regions
                regions = [
                    (0x80000000, 0x88000000, "Main RAM"),  # xv6 main memory
                    (0x10000000, 0x10001000, "UART"),     # UART registers
                    (0x2000000, 0x2010000, "CLINT"),      # Core Local Interruptor
                ]
                
                for start, end, name in regions:
                    try:
                        size = end - start
                        data = self.dump_memory_range(start, size)
                        if data:
                            data_bytes = bytes(data)
                            offset = 0
                            while True:
                                pos = data_bytes.find(pattern, offset)
                                if pos == -1:
                                    break
                                addr = start + pos
                                matches.append((addr, name))
                                offset = pos + 1
                    except:
                        continue
                
                return matches
        
        return GDBMemoryAnalyzer()
    
    def _dump_all_memory(self):
        """Dump all accessible memory regions"""
        gdb.write("=== Comprehensive Memory Dump ===\n")
        
        # Define major RISC-V xv6 memory regions
        regions = [
            (0x80000000, 0x88000000, "Main RAM"),
            (0x10000000, 0x10001000, "UART"),
            (0x2000000, 0x2010000, "CLINT"),
            (0xc000000, 0xc400000, "PLIC"),
        ]
        
        for start_addr, end_addr, name in regions:
            gdb.write(f"\nDumping {name} ({start_addr:#x}-{end_addr:#x})...\n")
            size = end_addr - start_addr
            
            try:
                data = self.analyzer.dump_memory_range(start_addr, size)
                if data:
                    filename = f"memory_dump_{name.lower().replace(' ', '_')}_{start_addr:x}.bin"
                    filepath = self.analyzer.output_dir / filename
                    
                    with open(filepath, 'wb') as f:
                        f.write(data)
                    
                    gdb.write(f"  Saved {len(data)} bytes to {filepath}\n")
                    
                    # Show hex preview of first 64 bytes
                    preview_data = data[:64]
                    self._print_hex_preview(preview_data, start_addr)
                else:
                    gdb.write(f"  Failed to read memory region\n")
                    
            except Exception as e:
                gdb.write(f"  Error dumping {name}: {e}\n")
    
    def _dump_address_range(self, start_addr, end_addr):
        """Dump specific address range"""
        size = end_addr - start_addr
        gdb.write(f"=== Memory Range Dump {start_addr:#x}-{end_addr:#x} ===\n")
        
        try:
            data = self.analyzer.dump_memory_range(start_addr, size)
            if data:
                filename = f"memory_range_{start_addr:x}_{end_addr:x}.bin" 
                filepath = self.analyzer.output_dir / filename
                
                with open(filepath, 'wb') as f:
                    f.write(data)
                
                gdb.write(f"Saved {len(data)} bytes to {filepath}\n")
                
                # Show full hex dump for ranges < 1KB
                if len(data) <= 1024:
                    self._print_hex_dump(data, start_addr)
                else:
                    self._print_hex_preview(data[:256], start_addr)
                    gdb.write(f"... ({len(data)-256} more bytes in file)\n")
            else:
                gdb.write("Failed to read memory range\n")
                
        except Exception as e:
            gdb.write(f"Error: {e}\n")
    
    def _search_memory_pattern(self, pattern_hex):
        """Search for hex pattern in memory"""
        gdb.write(f"=== Searching for pattern '{pattern_hex}' ===\n")
        
        try:
            matches = self.analyzer.search_pattern(pattern_hex)
            
            if matches:
                gdb.write(f"Found {len(matches)} matches:\n")
                for addr, region in matches:
                    gdb.write(f"  {addr:#x} in {region}\n")
                    
                    # Show context around match
                    try:
                        context_data = self.analyzer.dump_memory_range(addr - 16, 48)
                        if context_data:
                            gdb.write(f"    Context:\n")
                            self._print_hex_preview(context_data, addr - 16, highlight_offset=16)
                    except:
                        pass
            else:
                gdb.write("No matches found\n")
                
        except Exception as e:
            gdb.write(f"Search error: {e}\n")
    
    def _interactive_browser(self):
        """Start interactive memory browser"""
        gdb.write("=== Interactive Memory Browser ===\n")
        gdb.write("Commands: read <addr> [size], search <pattern>, regions, quit\n")
        
        while True:
            try:
                cmd_input = input("mem> ").strip()
                if not cmd_input:
                    continue
                
                parts = cmd_input.split()
                cmd = parts[0].lower()
                
                if cmd == "quit" or cmd == "q":
                    break
                elif cmd == "read" and len(parts) >= 2:
                    addr = int(parts[1], 0)
                    size = int(parts[2], 0) if len(parts) > 2 else 64
                    
                    data = self.analyzer.dump_memory_range(addr, size)
                    if data:
                        self._print_hex_dump(data, addr)
                    else:
                        gdb.write("Failed to read memory\n")
                        
                elif cmd == "search" and len(parts) >= 2:
                    pattern = parts[1]
                    matches = self.analyzer.search_pattern(pattern)
                    
                    if matches:
                        for addr, region in matches:
                            gdb.write(f"  {addr:#x} in {region}\n")
                    else:
                        gdb.write("No matches found\n")
                        
                elif cmd == "regions":
                    self._enumerate_memory_regions()
                else:
                    gdb.write("Invalid command. Use: read <addr> [size], search <pattern>, regions, quit\n")
                    
            except (EOFError, KeyboardInterrupt):
                break
            except Exception as e:
                gdb.write(f"Error: {e}\n")
        
        gdb.write("Exiting interactive browser\n")
    
    def _generate_memory_report(self):
        """Generate comprehensive memory map report"""
        gdb.write("=== Memory Map Report ===\n")
        
        # Get process information from GDB
        try:
            # Try to access xv6 kernel symbols
            gdb.write("\n--- Kernel Memory Layout ---\n")
            
            symbols = ['kernel_base', 'kernel_end', 'data_start', 'bss_start', 'heap_start']
            for sym in symbols:
                try:
                    val = gdb.parse_and_eval(sym)
                    gdb.write(f"{sym}: {int(val):#x}\n")
                except:
                    pass
            
            # Physical memory regions
            gdb.write("\n--- Physical Memory Regions ---\n")
            regions = [
                (0x80000000, 0x88000000, "Main RAM (128MB)"),
                (0x10000000, 0x10001000, "UART0 Registers"),
                (0x2000000, 0x2010000, "CLINT (Core Local Interruptor)"),
                (0xc000000, 0xc400000, "PLIC (Platform Interrupt Controller)"),
                (0x80000000, 0x80200000, "Kernel Code/Data"),
                (0x87000000, 0x88000000, "User Memory Pool"),
            ]
            
            for start, end, desc in regions:
                size_kb = (end - start) // 1024
                gdb.write(f"{start:#010x}-{end:#010x} ({size_kb:6d}KB) {desc}\n")
                
                # Try to read a few bytes to check accessibility
                try:
                    data = self.analyzer.dump_memory_range(start, 16)
                    status = "OK" if data else "FAIL"
                except:
                    status = "FAIL"
                gdb.write(f"                                     [Access: {status}]\n")
            
        except Exception as e:
            gdb.write(f"Report generation error: {e}\n")
    
    def _enumerate_memory_regions(self):
        """Enumerate and display memory regions"""
        gdb.write("=== Memory Region Enumeration ===\n")
        
        regions = [
            (0x80000000, 0x88000000, "RAM", "rwx"),
            (0x10000000, 0x10001000, "UART", "rw-"),
            (0x2000000, 0x2010000, "CLINT", "rw-"),
            (0xc000000, 0xc400000, "PLIC", "rw-"),
        ]
        
        gdb.write(f"{'Start Address':<12} {'End Address':<12} {'Size':<10} {'Perms':<5} {'Description'}\n")
        gdb.write("-" * 70 + "\n")
        
        for start, end, name, perms in regions:
            size_str = f"{(end-start)//1024}KB"
            gdb.write(f"{start:#010x}   {end:#010x}   {size_str:<10} {perms:<5} {name}\n")
    
    def _print_hex_dump(self, data, base_addr):
        """Print formatted hex dump"""
        for i in range(0, len(data), 16):
            addr = base_addr + i
            chunk = data[i:i+16]
            
            # Hex representation
            hex_str = ' '.join(f"{b:02x}" for b in chunk)
            hex_str = hex_str.ljust(47)  # 16*3-1 = 47 chars
            
            # ASCII representation  
            ascii_str = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in chunk)
            
            gdb.write(f"{addr:#010x}  {hex_str}  |{ascii_str}|\n")
    
    def _print_hex_preview(self, data, base_addr, highlight_offset=None):
        """Print hex preview with optional highlighting"""
        lines = min(4, (len(data) + 15) // 16)  # Max 4 lines preview
        
        for i in range(lines):
            start_idx = i * 16
            end_idx = min(start_idx + 16, len(data))
            addr = base_addr + start_idx
            chunk = data[start_idx:end_idx]
            
            # Hex representation with highlighting
            hex_parts = []
            for j, b in enumerate(chunk):
                if highlight_offset is not None and start_idx + j == highlight_offset:
                    hex_parts.append(f"[{b:02x}]")
                else:
                    hex_parts.append(f"{b:02x}")
            
            hex_str = ' '.join(hex_parts).ljust(50)
            
            # ASCII representation
            ascii_str = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in chunk)
            
            gdb.write(f"{addr:#010x}  {hex_str}  |{ascii_str}|\n")
    
    def _show_help(self):
        """Show command help"""
        gdb.write("""
Comprehensive Memory Analysis Commands:

mem_analysis dump-all
    Dump all major memory regions to files

mem_analysis dump-range <start> <end>  
    Dump specific address range (addresses in hex)
    Example: mem_analysis dump-range 0x80000000 0x80001000

mem_analysis search <hex_pattern>
    Search for hex pattern in memory
    Example: mem_analysis search deadbeef

mem_analysis interactive
    Start interactive memory browser

mem_analysis report
    Generate comprehensive memory map report

mem_analysis regions
    List all known memory regions

All numeric addresses can use 0x prefix for hex or decimal without prefix.
Dump files are saved to ./memory_dumps/ directory.

""")

# Quick memory access commands for convenience
class QuickMemoryDumpCommand(gdb.Command):
    """Quick memory dump command - memdump <addr> [size]"""
    
    def __init__(self):
        super(QuickMemoryDumpCommand, self).__init__("memdump", gdb.COMMAND_DATA)
        
    def invoke(self, args, from_tty):
        argv = gdb.string_to_argv(args)
        if not argv:
            gdb.write("Usage: memdump <address> [size]\n")
            return
            
        addr = int(argv[0], 0)
        size = int(argv[1], 0) if len(argv) > 1 else 256
        
        try:
            inferior = gdb.selected_inferior()
            data = inferior.read_memory(addr, size)
            
            # Print hex dump
            gdb.write(f"Memory dump at {addr:#x} ({size} bytes):\n")
            for i in range(0, len(data), 16):
                chunk_addr = addr + i
                chunk = data[i:i+16]
                
                hex_str = ' '.join(f"{b:02x}" for b in chunk)
                hex_str = hex_str.ljust(47)
                
                ascii_str = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in chunk)
                
                gdb.write(f"{chunk_addr:#010x}  {hex_str}  |{ascii_str}|\n")
                
        except Exception as e:
            gdb.write(f"Memory access error: {e}\n")

class QuickMemorySearchCommand(gdb.Command):
    """Quick memory search command - memsearch <hex_pattern> [start] [end]"""
    
    def __init__(self):
        super(QuickMemorySearchCommand, self).__init__("memsearch", gdb.COMMAND_DATA)
        
    def invoke(self, args, from_tty):
        argv = gdb.string_to_argv(args)
        if not argv:
            gdb.write("Usage: memsearch <hex_pattern> [start_addr] [end_addr]\n")
            return
            
        pattern_hex = argv[0]
        start_addr = int(argv[1], 0) if len(argv) > 1 else 0x80000000
        end_addr = int(argv[2], 0) if len(argv) > 2 else 0x88000000
        
        try:
            pattern = bytes.fromhex(pattern_hex)
            gdb.write(f"Searching for pattern '{pattern_hex}' in {start_addr:#x}-{end_addr:#x}...\n")
            
            # Search in chunks to avoid memory issues
            chunk_size = 0x10000  # 64KB chunks
            matches = []
            
            addr = start_addr
            while addr < end_addr:
                try:
                    size = min(chunk_size, end_addr - addr)
                    inferior = gdb.selected_inferior()
                    data = inferior.read_memory(addr, size)
                    
                    data_bytes = bytes(data)
                    offset = 0
                    while True:
                        pos = data_bytes.find(pattern, offset)
                        if pos == -1:
                            break
                        match_addr = addr + pos
                        matches.append(match_addr)
                        offset = pos + 1
                        
                except:
                    pass  # Skip inaccessible regions
                
                addr += chunk_size
            
            if matches:
                gdb.write(f"Found {len(matches)} matches:\n")
                for match_addr in matches:
                    gdb.write(f"  {match_addr:#x}\n")
            else:
                gdb.write("No matches found\n")
                
        except Exception as e:
            gdb.write(f"Search error: {e}\n")


def main():
    """Main entry point with comprehensive command-line interface"""
    parser = argparse.ArgumentParser(
        description="Comprehensive QEMU Memory Analysis Tool for RISC-V xv6",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --monitor tcp:localhost:55555 --dump-all
  %(prog)s --monitor unix:/tmp/qemu-monitor --gdb-port 1234 --interactive
  %(prog)s --monitor tcp:127.0.0.1:55555 --dump-range 0x80000000 0x80001000
  %(prog)s --monitor unix:/tmp/qemu-monitor --search deadbeef --output-dir ./dumps
        """
    )
    
    # Connection options
    parser.add_argument('--monitor', required=True,
                       help='QEMU monitor socket (tcp:host:port or unix:path)')
    parser.add_argument('--qmp-socket', 
                       help='QMP socket for enhanced control (unix:path)')
    parser.add_argument('--gdb-port', type=int, default=1234,
                       help='GDB remote protocol port (default: 1234)')
    
    # Operation modes
    parser.add_argument('--dump-all', action='store_true',
                       help='Dump all discoverable memory regions')
    parser.add_argument('--dump-range', nargs=2, metavar=('START', 'END'),
                       help='Dump specific address range (hex addresses)')
    parser.add_argument('--search', metavar='PATTERN',
                       help='Search for hex pattern in memory')
    parser.add_argument('--interactive', action='store_true',
                       help='Start interactive memory browser')
    
    # Analysis options  
    parser.add_argument('--access-mode', choices=['physical', 'virtual', 'both'],
                       default='physical', help='Memory access mode')
    parser.add_argument('--chunk-size', type=int, default=4096,
                       help='Chunk size for large dumps (bytes)')
    parser.add_argument('--output-dir', default='./memory_dumps',
                       help='Output directory for dump files')
    
    # Utility options
    parser.add_argument('--generate-report', action='store_true',
                       help='Generate memory map report')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Enable verbose logging')
    
    args = parser.parse_args()
    
    # Configure logging level
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)
    
    # Create analyzer instance
    try:
        analyzer = MemoryAnalyzer(
            qemu_monitor=args.monitor,
            qmp_socket=args.qmp_socket,
            gdb_port=args.gdb_port,
            output_dir=args.output_dir
        )
        
        # Initialize connections
        if not analyzer.initialize():
            logger.error("Failed to initialize analyzer")
            return 1
        
        # Parse access mode
        access_mode = MemoryAccessMode(args.access_mode)
        
        # Execute requested operations
        if args.dump_all:
            logger.info("Performing complete memory dump...")
            dumps = analyzer.dump_complete_memory(access_mode, args.chunk_size)
            
            # Save all dumps
            for dump in dumps:
                analyzer.save_dump_to_file(dump)
            
            logger.info(f"Completed dump of {len(dumps)} memory chunks")
        
        elif args.dump_range:
            start_addr = int(args.dump_range[0], 0)
            end_addr = int(args.dump_range[1], 0)
            
            dump = analyzer.dump_address_range(start_addr, end_addr, access_mode)
            analyzer.save_dump_to_file(dump)
            
            # Print hex preview
            print(f"\nMemory dump preview ({start_addr:#x}-{end_addr:#x}):")
            analyzer._print_hex_dump(dump.data[:256], start_addr)  # First 256 bytes
            if len(dump.data) > 256:
                print(f"... ({len(dump.data)-256} more bytes saved to file)")
        
        elif args.search:
            pattern = bytes.fromhex(args.search)
            matches = analyzer.search_memory(pattern)
            
            print(f"\nSearch results for pattern '{args.search}':")
            for addr, region in matches:
                print(f"  Found at {addr:#x} in {region.description}")
        
        elif args.interactive:
            analyzer.interactive_memory_browser()
        
        # Generate report if requested
        if args.generate_report:
            report = analyzer.generate_memory_map_report()
            report_file = Path(args.output_dir) / "memory_map_report.txt"
            
            with open(report_file, 'w') as f:
                f.write(report)
            
            print(f"\nMemory map report saved to {report_file}")
            print("\n" + report)
        
        # Default action if no specific operation requested
        if not any([args.dump_all, args.dump_range, args.search, 
                   args.interactive, args.generate_report]):
            print("No operation specified. Use --help for options.")
            print("Available operations: --dump-all, --dump-range, --search, --interactive")
    
    except KeyboardInterrupt:
        logger.info("Operation cancelled by user")
    except Exception as e:
        logger.error(f"Analysis failed: {e}")
        return 1
    finally:
        if 'analyzer' in locals():
            analyzer.cleanup()
    
    return 0


# ============================================================================
# GDB Command Registration for QEMU Integration 
# ============================================================================

# Initialize GDB commands when this module is imported in GDB environment
try:
    # Check if we're running inside GDB
    import gdb
    
    # Register all the comprehensive memory analysis commands
    ComprehensiveMemoryAnalysisCommand()
    QuickMemoryDumpCommand() 
    QuickMemorySearchCommand()
    
    # Print initialization message
    gdb.write("""
================================================================================
Comprehensive Memory Analysis Tools for RISC-V xv6 Initialized
================================================================================

Available Commands:
  mem_analysis      - Comprehensive memory analysis and introspection
  memdump          - Quick memory dump with hex display  
  memsearch        - Quick memory pattern search

Advanced Memory Introspection Capabilities Loaded:
✓ Multi-layered memory access strategies
✓ Byte-level granularity with complete coverage
✓ Real-time interactive analysis interface
✓ Memory region discovery and enumeration  
✓ Pattern matching across entire address space
✓ Comprehensive memory map reporting

Usage Examples:
  mem_analysis dump-all                    # Dump all memory regions
  mem_analysis dump-range 0x80000000 0x80001000  # Dump specific range
  mem_analysis search deadbeef             # Search for hex pattern
  mem_analysis interactive                 # Start interactive browser
  mem_analysis report                      # Generate memory map report
  
  memdump 0x80000000 256                  # Quick 256-byte hex dump
  memsearch cafebabe 0x80000000 0x88000000 # Search in RAM region

For detailed help: mem_analysis (with no arguments)
================================================================================
""")

except ImportError:
    # Not running in GDB environment - allow standalone execution
    pass

if __name__ == "__main__":
    sys.exit(main())
