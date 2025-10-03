#!/usr/bin/env python3
"""
Dump xv6 proc[] table from a running QEMU monitor (telnet).

This enhanced version reads per-slot: state, pid, pagetable (satp), and name[16].

Usage:
  python3 tools/dump_proc_table.py --host 127.0.0.1 --port 4444

The script will try to read `kernel/kernel.sym` to find the `proc` symbol for the base
address. If not found, pass --base.

Defaults (inferred from this tree):
  stride = 0x160
  state_offset = 0x90

This script only reads memory words via the QEMU monitor `x/` command and does not
require gdb.
"""
import telnetlib
import re
import argparse
import time
import os
import sys

STATE_NAMES = {
    0: 'UNUSED',
    1: 'USED',
    2: 'SLEEPING',
    3: 'RUNNABLE',
    4: 'RUNNING',
    5: 'ZOMBIE'
}

DEFAULT_STRIDE = 0x160
DEFAULT_STATE_OFFSET = 0x90

KERNEL_SYM_PATH = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'kernel', 'kernel.sym')

sym_re = re.compile(r"^([0-9a-fA-F]+)\s+proc$")
addr_re = re.compile(r"0x?([0-9a-fA-F]+)")


def parse_kernel_sym(path):
    try:
        with open(path, 'r') as f:
            for ln in f:
                ln = ln.strip()
                m = sym_re.search(ln)
                if m:
                    return int(m.group(1), 16)
    except FileNotFoundError:
        return None
    return None


def read_until_prompt(tn, prompt=b"(qemu)"):
    # Read until the qemu prompt appears. Return all data read (bytes)
    data = b""
    while True:
        try:
            chunk = tn.read_until(b"\n", timeout=1)
        except EOFError:
            break
        if not chunk:
            break
        data += chunk
        if prompt in chunk:
            break
    return data


def send_cmd(tn, cmd):
    if not cmd.endswith('\n'):
        cmd += '\n'
    tn.write(cmd.encode('ascii'))
    # wait briefly for response to arrive
    time.sleep(0.02)
    return read_until_prompt(tn)


def parse_x_output_for_addr(output):
    # output is bytes, look for first hex word after ':'
    s = output.decode('ascii', errors='ignore')
    # find 0x... patterns
    m = re.search(r":\s*0x([0-9a-fA-F]+)", s)
    if not m:
        # fallback: any hex in the text
        m2 = addr_re.search(s)
        if not m2:
            return None
        return int(m2.group(1), 16)
    return int(m.group(1), 16)


def read_c_string(tn, addr, maxlen=16):
    # Read the name[16] as bytes by reading successive 8-byte words
    # and concatenating until NUL or maxlen reached.
    name_bytes = bytearray()
    words = (maxlen + 7) // 8
    for i in range(words):
        a = addr + i*8
        out = send_cmd(tn, f"x/1gx 0x{a:x}")
        v = parse_x_output_for_addr(out)
        if v is None:
            break
        # convert 64-bit word to bytes (little endian)
        for b in v.to_bytes(8, 'little'):
            if len(name_bytes) >= maxlen:
                break
            if b == 0:
                return name_bytes.decode('ascii', errors='ignore')
            name_bytes.append(b)
    return name_bytes.decode('ascii', errors='ignore')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--host', default='127.0.0.1')
    p.add_argument('--port', type=int, default=4444)
    p.add_argument('--base', type=lambda x: int(x,0), default=None,
                   help='proc base address (hex)')
    p.add_argument('--stride', type=lambda x: int(x,0), default=DEFAULT_STRIDE,
                   help='stride (bytes) between proc entries')
    p.add_argument('--offset', type=lambda x: int(x,0), default=DEFAULT_STATE_OFFSET,
                   help='offset (bytes) of state field within struct proc')
    p.add_argument('--count', type=int, default=64, help='number of proc slots')
    p.add_argument('--out', type=str, default=None, help='Optional output file to write dump')
    args = p.parse_args()

    base = args.base
    if base is None:
        base = parse_kernel_sym(KERNEL_SYM_PATH)
        if base is None:
            print(f"Couldn't auto-detect proc base from {KERNEL_SYM_PATH}. Provide --base.", file=sys.stderr)
            sys.exit(1)
        else:
            print(f"Detected proc base from kernel.sym: 0x{base:x}")

    try:
        tn = telnetlib.Telnet(args.host, args.port, timeout=5)
    except Exception as e:
        print(f"Failed to connect to {args.host}:{args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    # Read banner/prompt
    banner = read_until_prompt(tn)

    results = []
    # Offsets inside struct proc (from kernel/proc.h)
    # state_offset is provided; pid_offset and pagetable_offset based on source layout
    # From proc.h: after state(1 byte/enum) and some ints, pid is after xstate etc.
    # Using hard-coded offsets matched to the repo's proc.h layout:
    PID_OFFSET = 0x9c  # offset of pid within struct proc (empirically)
    PAGETABLE_OFFSET = 0xb0  # offset of pagetable within struct proc (empirically)
    NAME_OFFSET = 0xe8  # offset of name[16] within struct proc (empirically)

    for i in range(args.count):
        state_addr = base + i * args.stride + args.offset
        out = send_cmd(tn, f"x/1gx 0x{state_addr:x}")
        state_val = parse_x_output_for_addr(out)
        state_name = STATE_NAMES.get(state_val, f'UNKNOWN({state_val})') if state_val is not None else 'INVALID'

        entry = {'index': i, 'state_val': state_val, 'state_name': state_name}

        if state_val is not None and state_val != 0:
            # read pid
            pid_addr = base + i * args.stride + PID_OFFSET
            out = send_cmd(tn, f"x/1gx 0x{pid_addr:x}")
            pid_word = parse_x_output_for_addr(out)
            pid = pid_word & 0xffffffff if pid_word is not None else None
            entry['pid'] = pid

            # read pagetable (pagetable_t, a 64-bit value)
            pagetable_addr = base + i * args.stride + PAGETABLE_OFFSET
            out = send_cmd(tn, f"x/1gx 0x{pagetable_addr:x}")
            pagetable = parse_x_output_for_addr(out)
            entry['pagetable'] = pagetable

            # read name
            name_addr = base + i * args.stride + NAME_OFFSET
            name = read_c_string(tn, name_addr, maxlen=16)
            entry['name'] = name

        results.append(entry)

    # Print table
    lines = []
    lines.append("Idx  State     PID    Pagetable           Name")
    lines.append("---  --------  -----  -------------------  ----------------")
    for e in results:
        if e['state_name'] == 'UNUSED':
            lines.append(f"{e['index']:3d}  {e['state_name']:8s}")
        else:
            lines.append(f"{e['index']:3d}  {e['state_name']:8s}  {str(e.get('pid') or ''):5s}  {('0x%016x' % e.get('pagetable')) if e.get('pagetable') else '':19s}  {e.get('name','')}")

    out_text = "\n".join(lines)
    if args.out:
        with open(args.out, 'w') as f:
            f.write(out_text)
        print(f"Wrote proc table to {args.out}")
    else:
        print(out_text)

    tn.close()


if __name__ == '__main__':
    main()
