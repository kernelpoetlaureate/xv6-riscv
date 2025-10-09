#!/usr/bin/env python3
"""
capture_and_process.py

Runs a command (for example the qemu invocation used to run xv6), captures
its stdout/stderr into a timestamped raw log file, and then runs
`parse_qemu_log.py` on the captured log to produce formatted outputs.

Usage:
  python3 tools/capture_and_process.py -- cmd args...

Example (simulate xv6 output capture using the example file):
  python3 tools/capture_and_process.py -- cat kernel/data.md

Output:
- tools/raw/<timestamp>.log  (raw captured console output)
- tools/output-<timestamp>.md
- tools/output-<timestamp>-kfree.csv

Note: On Windows/WSL you may need to run under WSL shell. The script is
portable and does not invoke qemu directly; it simply runs the provided
command and captures its console output.
"""
import argparse
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def cleanup_old_files(out_dir):
    """Remove old output files and raw logs to keep workspace clean"""
    out_base = Path(out_dir)
    
    # Patterns for files to clean up
    patterns = [
        'output-*T*Z.md',           # Timestamped markdown files
        'output-*T*Z-kfree.csv',   # Timestamped CSV files
        'test-unified*.md',         # Test files
        'test-unified*.csv',        # Test CSV files
        'unified-test*.md',         # More test files
        'unified-test*.csv',        # More test CSV files
    ]
    
    print("🧹 Cleaning up old output files...")
    removed_count = 0
    
    for pattern in patterns:
        for file_path in out_base.glob(pattern):
            try:
                file_path.unlink()
                print(f"  Removed: {file_path.name}")
                removed_count += 1
            except OSError as e:
                print(f"  Warning: Could not remove {file_path.name}: {e}")
    
    # Clean up old raw logs (keep only last 3 for safety)
    raw_dir = out_base / 'raw'
    if raw_dir.exists():
        raw_logs = sorted(raw_dir.glob('*T*Z.log'), key=lambda p: p.stat().st_mtime, reverse=True)
        if len(raw_logs) > 3:
            for old_log in raw_logs[3:]:  # Keep newest 3, remove rest
                try:
                    old_log.unlink()
                    print(f"  Removed old log: {old_log.name}")
                    removed_count += 1
                except OSError as e:
                    print(f"  Warning: Could not remove {old_log.name}: {e}")
    
    if removed_count > 0:
        print(f"✅ Cleaned up {removed_count} old files\n")
    else:
        print("✅ No old files to clean up\n")


def run_and_capture(cmd, raw_path, timeout=30):
    # Run the command, capture stdout+stderr, write to raw_path
    print(f"Running: {' '.join(cmd)} (timeout: {timeout}s)")
    import threading
    import time
    
    def kill_proc_after_timeout(proc, timeout):
        time.sleep(timeout)
        if proc.poll() is None:
            print(f"\nTimeout after {timeout}s, terminating process...")
            proc.terminate()
            time.sleep(2)
            if proc.poll() is None:
                proc.kill()
    
    with raw_path.open('wb') as f:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        
        # Start timeout thread
        timeout_thread = threading.Thread(target=kill_proc_after_timeout, args=(proc, timeout))
        timeout_thread.daemon = True
        timeout_thread.start()
        
        try:
            for chunk in iter(lambda: proc.stdout.read(1024), b""):
                f.write(chunk)
                f.flush()
                # also echo to console
                sys.stdout.buffer.write(chunk)
                sys.stdout.flush()
        except KeyboardInterrupt:
            print("\nInterrupted by user, terminating process...")
            proc.terminate()
            time.sleep(2)
            if proc.poll() is None:
                proc.kill()
        
        proc.wait()
    return proc.returncode


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-dir', default='tools', help='output base dir')
    parser.add_argument('--run-xv6', action='store_true', help='run `make clean && make qemu` and capture its output')
    parser.add_argument('--no-cleanup', action='store_true', help='skip cleanup of old files')
    parser.add_argument('cmd', nargs=argparse.REMAINDER, help='command to run (prefix with --)')
    args = parser.parse_args(argv[1:])

    # Clean up old files unless --no-cleanup is specified
    if not args.no_cleanup:
        cleanup_old_files(args.out_dir)

    # If --run-xv6 passed, run the typical build+qemu pipeline under a shell.
    if args.run_xv6:
        # Note: we run via the shell so '&&' works. This will use the system shell
        # (on WSL that's bash). The subprocess invocation in run_and_capture expects a
        # list of args; to allow shell features we pass ['bash', '-lc', '<cmd>'].
        cmd = ['bash', '-lc', 'make clean && make qemu']
    else:
        # argparse.REMAINDER may include a leading '--' when called using '-- cmd...'
        cmd = args.cmd
        if cmd and cmd[0] == '--':
            cmd = cmd[1:]

        if not cmd:
            print('Usage: capture_and_process.py [--run-xv6] -- <command>')
            return 2

    # use timezone-aware UTC timestamp
    ts = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    out_base = Path(args.out_dir)
    raw_dir = out_base / 'raw'
    raw_dir.mkdir(parents=True, exist_ok=True)

    raw_path = raw_dir / f'{ts}.log'
    # Use longer timeout for QEMU (60 seconds) vs regular commands (30 seconds)
    timeout = 60 if args.run_xv6 else 30
    rc = run_and_capture(cmd, raw_path, timeout)
    print(f'Raw output saved to {raw_path} (rc={rc})')

    # Now process with the parser we added earlier
    parser_script = Path('tools/parse_qemu_log.py')
    if not parser_script.exists():
        print('Error: parse_qemu_log.py not found in tools/; cannot process')
        return 3

    md_out = out_base / f'output-{ts}.md'
    csv_out = out_base / f'output-{ts}-kfree.csv'

    subprocess.check_call([sys.executable, str(parser_script), str(raw_path), str(out_base / f'output-{ts}')])

    print(f'Processed output: {md_out} and {csv_out}')
    return rc


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
