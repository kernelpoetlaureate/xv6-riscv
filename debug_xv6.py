
import subprocess
import sys
import time
import os
import argparse
from pathlib import Path

class XV6DebugAutomation:
    def __init__(self, project_dir=None):
        """Initialize the debugging automation framework."""
        self.project_dir = Path(project_dir) if project_dir else Path.cwd()
        self.tmux_session_name = "xv6-debug"
        self.gdb_port = 26000
        
    def check_prerequisites(self):
        """Verify all required tools are installed."""
        print("[*] Checking prerequisites...")
        
        required = {
            'make': 'Build system',
            'qemu-system-riscv64': 'RISC-V emulator',
            'gdb-multiarch': 'Multi-architecture debugger',
            'tmux': 'Terminal multiplexer'
        }
        
        missing = []
        for tool, description in required.items():
            result = subprocess.run(['which', tool], 
                                  capture_output=True, 
                                  text=True)
            if result.returncode != 0:
                missing.append(f"  - {tool} ({description})")
                print(f"    [!] Missing: {tool}")
            else:
                print(f"    [✓] Found: {tool}")
        
        if missing:
            print("\n[ERROR] Missing required tools:")
            for item in missing:
                print(item)
            print("\nInstall with:")
            print("  sudo apt-get install qemu-system-misc gdb-multiarch tmux")
            return False
        
        return True
    
    def kill_existing_session(self):
        """Kill any existing xv6 debug session."""
        print("[*] Cleaning up existing sessions...")
        
        # Kill tmux session
        subprocess.run(['tmux', 'kill-session', '-t', self.tmux_session_name],
                      stderr=subprocess.DEVNULL)
        
        # Kill any lingering QEMU processes
        subprocess.run(['pkill', '-f', 'qemu-system-riscv64.*xv6'],
                      stderr=subprocess.DEVNULL)
        
        time.sleep(0.5)
    
    def create_tmux_session(self):
        """Create tmux session with QEMU and GDB panes."""
        print("[*] Creating tmux session...")
        
        # Create new detached session with QEMU pane
        subprocess.run([
            'tmux', 'new-session', '-d',
            '-s', self.tmux_session_name,
            '-n', 'debug',
            '-c', str(self.project_dir)
        ], check=True)
        
        # Split window vertically (QEMU left, GDB right)
        subprocess.run([
            'tmux', 'split-window', '-h',
            '-t', f'{self.tmux_session_name}:debug',
            '-c', str(self.project_dir)
        ], check=True)
        
        # Adjust pane sizes (60% QEMU, 40% GDB)
        subprocess.run([
            'tmux', 'resize-pane', '-t', f'{self.tmux_session_name}:debug.0',
            '-x', '60%'
        ], check=True)
        
        print("    [✓] Created session with QEMU (left) and GDB (right) panes")
    
    def start_qemu(self):
        """Launch QEMU in debug mode in the QEMU pane."""
        print("[*] Starting QEMU in debug mode...")
        
        qemu_cmd = 'make qemu-gdb'
        
        # Send command to QEMU pane (pane 0)
        subprocess.run([
            'tmux', 'send-keys', '-t', f'{self.tmux_session_name}:debug.0',
            qemu_cmd, 'Enter'
        ], check=True)
        
        # Wait for QEMU to start listening on GDB port
        print(f"    [*] Waiting for QEMU to listen on port {self.gdb_port}...")
        max_wait = 10
        for i in range(max_wait):
            time.sleep(1)
            result = subprocess.run(
                ['netstat', '-tuln'],
                capture_output=True,
                text=True
            )
            if f':{self.gdb_port}' in result.stdout:
                print(f"    [✓] QEMU listening on port {self.gdb_port}")
                return True
        
        print(f"    [!] Warning: Could not verify QEMU port (timeout)")
        return True
    
    def start_gdb(self, auto_commands=None):
        """Launch GDB and execute initial commands."""
        print("[*] Starting GDB...")
        
        # Build GDB command sequence
        gdb_init_commands = [
            'gdb-multiarch kernel/kernel',
        ]
        
        # Send GDB launch command to GDB pane (pane 1)
        subprocess.run([
            'tmux', 'send-keys', '-t', f'{self.tmux_session_name}:debug.1',
            gdb_init_commands[0], 'Enter'
        ], check=True)
        
        # Wait for GDB to initialize
        time.sleep(2)
        
        if auto_commands:
            print("[*] Executing initial GDB commands...")
            for cmd in auto_commands:
                print(f"    > {cmd}")
                subprocess.run([
                    'tmux', 'send-keys', '-t', f'{self.tmux_session_name}:debug.1',
                    cmd, 'Enter'
                ], check=True)
                time.sleep(0.3)
        
        print("    [✓] GDB ready")
    
    def attach_to_session(self):
        """Attach to the tmux session."""
        print("\n" + "="*60)
        print("XV6 DEBUG ENVIRONMENT READY")
        print("="*60)
        print(f"\nTmux session: {self.tmux_session_name}")
        print("\nLayout:")
        print("  Left pane:  QEMU (xv6 console)")
        print("  Right pane: GDB (debugger)")
        print("\nAttaching to session...")
        print("\nTo detach: Press Ctrl+B, then D")
        print("To reattach later: tmux attach -t xv6-debug")
        print("To kill session: tmux kill-session -t xv6-debug")
        print("="*60 + "\n")
        
        time.sleep(1)
        
        # Attach to session (blocking call)
        subprocess.run([
            'tmux', 'attach-session',
            '-t', self.tmux_session_name
        ])
    
    def run(self, auto_commands=None, no_attach=False):
        """Execute the complete debugging setup workflow."""
        print("="*60)
        print("XV6 RISC-V AUTOMATED DEBUG ENVIRONMENT")
        print("="*60 + "\n")
        
        # Step 1: Prerequisites check
        if not self.check_prerequisites():
            return False
        
        # Step 2: Clean up existing sessions
        self.kill_existing_session()
        
        # Step 3: Create tmux layout
        self.create_tmux_session()
        
        # Step 4: Start QEMU
        if not self.start_qemu():
            print("[ERROR] Failed to start QEMU")
            return False
        
        # Step 5: Start GDB with auto-commands
        self.start_gdb(auto_commands)
        
        # Step 6: Attach to session (unless no_attach flag set)
        if not no_attach:
            self.attach_to_session()
        else:
            print(f"\n[*] Session created but not attached.")
            print(f"    To attach: tmux attach -t {self.tmux_session_name}")
        
        return True


def main():
    parser = argparse.ArgumentParser(
        description='Automated xv6 RISC-V debugging environment',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic usage - full automated setup
  ./debug_xv6.py
  
  # Setup with custom breakpoints
  ./debug_xv6.py --break syscall --break scheduler
  
  # Quick inspection mode
  ./debug_xv6.py --quick
  
  # Create session but don't attach (for scripting)
  ./debug_xv6.py --no-attach
        """
    )
    
    parser.add_argument('--dir', type=str, default='.',
                       help='xv6 project directory (default: current directory)')
    
    parser.add_argument('--break', '-b', action='append', dest='breakpoints',
                       help='Set breakpoint (can be used multiple times)')
    
    parser.add_argument('--quick', '-q', action='store_true',
                       help='Quick mode: break at syscall and continue')
    
    parser.add_argument('--no-attach', action='store_true',
                       help='Create session but do not attach')
    
    parser.add_argument('--port', type=int, default=26000,
                       help='GDB port (default: 26000)')
    
    args = parser.parse_args()
    
    # Build auto-commands list
    auto_commands = []
    
    # Quick mode: common debugging scenario
    if args.quick:
        auto_commands.extend([
            'break syscall',
            'continue',
            'dump-kernel',
        ])
    elif args.breakpoints:
        # Custom breakpoints
        for bp in args.breakpoints:
            auto_commands.append(f'break {bp}')
        auto_commands.append('continue')
    else:
        # Default: just break at syscall
        auto_commands.extend([
            'break syscall',
        ])
    
    # Create and run automation
    automation = XV6DebugAutomation(project_dir=args.dir)
    automation.gdb_port = args.port
    
    success = automation.run(
        auto_commands=auto_commands,
        no_attach=args.no_attach
    )
    
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
