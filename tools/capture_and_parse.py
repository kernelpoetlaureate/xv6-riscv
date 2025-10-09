#!/usr/bin/env python3
"""
capture_and_parse.py

Unified script that both captures command output and parses/prettifies it.
Combines the functionality of capture_and_process.py and parse_qemu_log.py.

Usage:
  python3 tools/capture_and_parse.py [--run-xv6] -- <command>
  python3 tools/capture_and_parse.py --parse-only <log-file> [output-prefix]

Examples:
  # Capture xv6 output and parse it
  python3 tools/capture_and_parse.py --run-xv6
  
  # Capture any command output and parse it
  python3 tools/capture_and_parse.py -- cat kernel/data.md
  
  # Just parse an existing log file
  python3 tools/capture_and_parse.py --parse-only tools/raw/20251009T092317Z.log

Output:
- tools/raw/<timestamp>.log  (raw captured console output)
- tools/output-<timestamp>.md (formatted markdown)
- tools/output-<timestamp>-kfree.csv (CSV data)
"""
import argparse
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def run_and_capture(cmd, raw_path):
    """Run the command, capture stdout+stderr, write to raw_path"""
    print(f"Running: {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    with raw_path.open('wb') as f:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for chunk in iter(lambda: proc.stdout.read(1024), b""):
            f.write(chunk)
            f.flush()
            # also echo to console
            sys.stdout.buffer.write(chunk)
            sys.stdout.flush()
        proc.wait()
    return proc.returncode


def read_input(path):
    """Read log file with proper encoding handling"""
    return Path(path).read_text(encoding="utf-8", errors="replace")


def extract_kfree(text):
    """Extract kfree lines like: kfree: freeing page at 0x0000000080025000"""
    pattern = re.compile(r"kfree: freeing page at (0x[0-9a-fA-F]+)")
    return pattern.findall(text)


def extract_proc_table(text):
    """Extract proc table lines like: proc[0] addr=0x... pid=0 state=UNUSED kstack=0x..."""
    pattern = re.compile(r"proc\[(\d+)\]\s+addr=(0x[0-9a-fA-F]+)\s+pid=(\d+)\s+state=(\S+)\s+kstack=(0x[0-9a-fA-F]+)")
    return [m.groups() for m in pattern.finditer(text)]


def extract_syscalls(text):
    """Extract syscall traces and return values"""
    # lines like: PID 1: SYSCALL 15 (open) - Heap end(sz)=0x4000 User SP=0x3fb0
    pattern = re.compile(r"PID\s+(\d+):\s+SYSCALL\s+(\d+)\s+\(([^)]+)\)\s+-\s+Heap end\(sz\)=([^\s]+)\s+User SP=([^\s]+)")
    calls = []
    for m in pattern.finditer(text):
        pid, num, name, heap, sp = m.groups()
        calls.append({"pid": int(pid), "num": int(num), "name": name, "heap": heap, "sp": sp})
    
    # also extract RETURN lines that follow
    returns = re.findall(r"PID\s+(\d+):\s+RETURN\s+([^\s]+)\s+\((0x[0-9a-fA-F]+)\)", text)
    retmap = {}
    for pid, val, hexv in returns:
        retmap.setdefault(int(pid), []).append({"val": val, "hex": hexv})
    return calls, retmap


def to_markdown(kfrees, procs, syscalls, returns):
    """Generate formatted markdown output"""
    out = []
    out.append("# Parsed QEMU / Kernel Log\n")

    out.append("## kfree (freed pages)\n")
    out.append("| Index | Address |\n|---:|:---:|")
    for i, a in enumerate(kfrees):
        out.append(f"| {i} | `{a}` |")

    out.append("\n## Proc Table (summary)\n")
    out.append("| Index | Addr | PID | State | Kstack |\n|---:|:---:|:---:|:---:|:---:|")
    for idx, addr, pid, state, kstack in procs:
        out.append(f"| {idx} | `{addr}` | {pid} | {state} | `{kstack}` |")

    out.append("\n## Syscalls (detected)\n")
    out.append("| PID | Num | Name | HeapEnd | UserSP | Returns |\n|---:|---:|:---:|:---:|:---:|:---:|")
    for s in syscalls:
        pid = s['pid']
        r = returns.get(pid, [])
        rstr = "; ".join([f"{it['val']} ({it['hex']})" for it in r]) if r else ""
        out.append(f"| {pid} | {s['num']} | {s['name']} | `{s['heap']}` | `{s['sp']}` | {rstr} |")

    return "\n".join(out)


def to_csv_kfree(kfrees):
    """Generate CSV output for kfree data"""
    lines = ["index,address"]
    for i, a in enumerate(kfrees):
        lines.append(f"{i},{a}")
    return "\n".join(lines)


def parse_log(log_path, out_prefix):
    """Parse a log file and generate formatted outputs"""
    text = read_input(log_path)

    kfrees = extract_kfree(text)
    procs = extract_proc_table(text)
    syscalls, returns = extract_syscalls(text)

    md = to_markdown(kfrees, procs, syscalls, returns)
    Path(out_prefix + ".md").write_text(md, encoding="utf-8")

    csv_kfree = to_csv_kfree(kfrees)
    Path(out_prefix + "-kfree.csv").write_text(csv_kfree, encoding="utf-8")

    print(f"Wrote {out_prefix}.md and {out_prefix}-kfree.csv")
    return 0


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-dir', default='tools', help='output base dir')
    parser.add_argument('--run-xv6', action='store_true', help='run `make clean && make qemu` and capture its output')
    parser.add_argument('--parse-only', metavar='LOG_FILE', help='only parse an existing log file (no capture)')
    parser.add_argument('cmd', nargs=argparse.REMAINDER, help='command to run (prefix with --)')
    args = parser.parse_args(argv[1:])

    # Parse-only mode
    if args.parse_only:
        log_file = args.parse_only
        if not Path(log_file).exists():
            print(f"Error: Log file {log_file} not found")
            return 1
        
        # If there's remaining args after --parse-only, use the first as output prefix
        out_prefix = args.cmd[0] if args.cmd else "tools/output"
        return parse_log(log_file, out_prefix)

    # Capture mode
    if args.run_xv6:
        # Run the typical build+qemu pipeline under a shell
        cmd = ['bash', '-lc', 'make clean && make qemu']
    else:
        # argparse.REMAINDER may include a leading '--' when called using '-- cmd...'
        cmd = args.cmd
        if cmd and cmd[0] == '--':
            cmd = cmd[1:]

        if not cmd:
            print('Usage: capture_and_parse.py [--run-xv6] [--parse-only LOG_FILE] -- <command>')
            return 2

    # use timezone-aware UTC timestamp
    ts = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    out_base = Path(args.out_dir)
    raw_dir = out_base / 'raw'
    raw_dir.mkdir(parents=True, exist_ok=True)

    raw_path = raw_dir / f'{ts}.log'
    rc = run_and_capture(cmd, raw_path)
    print(f'Raw output saved to {raw_path} (rc={rc})')

    # Now parse the captured log
    out_prefix = str(out_base / f'output-{ts}')
    parse_log(str(raw_path), out_prefix)

    print(f'Processed output: {out_prefix}.md and {out_prefix}-kfree.csv')
    return rc


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))