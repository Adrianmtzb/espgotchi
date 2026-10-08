#!/usr/bin/env bash
# Serial console (type 'help' for commands). Usage: scripts/monitor.sh [/dev/cu.usbmodemXXXX]
set -euo pipefail
PORT="${1:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "no /dev/cu.usbmodem* port found"; exit 1; }
exec arduino-cli monitor -p "$PORT" -c baudrate=115200
