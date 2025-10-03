#!/usr/bin/env bash

# run_proc_dump.sh
# Waits for QEMU telnet monitor port and runs the process table dump script

HOST="localhost"
PORT=4444
MAX_ATTEMPTS=20
DELAY=1
DUMP_SCRIPT="tools/dump_proc_table.py"
OUTPUT_FILE=""
COLOR="auto"

# Process command-line arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --host)
      HOST="$2"
      shift 2
      ;;
    --port)
      PORT="$2"
      shift 2
      ;;
    --attempts)
      MAX_ATTEMPTS="$2"
      shift 2
      ;;
    --delay)
      DELAY="$2"
      shift 2
      ;;
    --output)
      OUTPUT_FILE="$2"
      shift 2
      ;;
    --color)
      COLOR="$2"
      shift 2
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--host HOST] [--port PORT] [--attempts MAX_ATTEMPTS] [--delay DELAY_SECONDS] [--output OUTPUT_FILE] [--color yes|no|auto]"
      exit 1
      ;;
  esac
done

# Configure color mode
if [[ "$COLOR" == "no" ]]; then
  export NO_COLOR=1
fi

# Function to check if port is open
check_port() {
  local host=$1
  local port=$2
  
  # Try with nc (netcat) first
  if command -v nc >/dev/null 2>&1; then
    nc -z $host $port >/dev/null 2>&1
    return $?
  fi
  
  # Fall back to timeout + telnet
  if command -v timeout >/dev/null 2>&1 && command -v telnet >/dev/null 2>&1; then
    timeout 1 telnet $host $port >/dev/null 2>&1
    return $?
  fi
  
  # Fall back to pure bash TCP check
  if command -v bash >/dev/null 2>&1; then
    (echo > /dev/tcp/$host/$port) >/dev/null 2>&1
    return $?
  fi
  
  echo "No way to check port connectivity. Install nc, telnet, or use a newer bash." >&2
  return 1
}

# Print status message
echo "Waiting for QEMU monitor at ${HOST}:${PORT}..."

# Wait for the port to become available
attempts=0
while [ $attempts -lt $MAX_ATTEMPTS ]; do
  if check_port $HOST $PORT; then
    echo "QEMU monitor found at ${HOST}:${PORT}"
    
    # Build the command
    CMD="python3 $DUMP_SCRIPT --host $HOST --port $PORT"
    if [ ! -z "$OUTPUT_FILE" ]; then
      CMD="$CMD --out $OUTPUT_FILE"
    fi
    
    # Run the process dump script
    echo "Running: $CMD"
    $CMD
    exit $?
  fi
  
  attempts=$((attempts + 1))
  echo "Attempt $attempts/$MAX_ATTEMPTS - QEMU monitor not available, waiting ${DELAY}s..."
  sleep $DELAY
done

echo "Timed out waiting for QEMU monitor at ${HOST}:${PORT}" >&2
exit 1