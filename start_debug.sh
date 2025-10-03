#!/bin/bash

# Simple script to completely stop all QEMU processes and start debug environment

echo "==== CLEANING UP QEMU PROCESSES ===="
# Kill any QEMU processes
pkill -9 -f qemu-system-riscv64 || true
pkill -9 -f 'qemu.*fs.img' || true

# Run the stop-qemu task
make stop-qemu || true

# Wait for resources to be released
echo "Waiting for resources to be released..."
sleep 2

# Start the debug environment
echo "==== STARTING DEBUG ENVIRONMENT ===="
python3 debug_xv6.py "$@"