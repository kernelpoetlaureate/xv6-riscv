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


def run_and_capture(cmd, raw_path):
    # Run the command, capture stdout+stderr, write to raw_path
    print(f"Running: {' '.join(cmd)}")
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


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument('--out-dir', default='tools', help='output base dir')
    parser.add_argument('cmd', nargs=argparse.REMAINDER, help='command to run (prefix with --)')
    args = parser.parse_args(argv[1:])

    # argparse.REMAINDER may include a leading '--' when called using '-- cmd...'
    cmd = args.cmd
    if cmd and cmd[0] == '--':
        cmd = cmd[1:]

    if not cmd:
        print('Usage: capture_and_process.py -- <command>')
        return 2

    # use timezone-aware UTC timestamp
    ts = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    out_base = Path(args.out_dir)
    raw_dir = out_base / 'raw'
    raw_dir.mkdir(parents=True, exist_ok=True)

    raw_path = raw_dir / f'{ts}.log'
    rc = run_and_capture(cmd, raw_path)
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
