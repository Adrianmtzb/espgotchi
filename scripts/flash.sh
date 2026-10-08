#!/usr/bin/env bash
# Builds and uploads to the board. Usage: scripts/flash.sh [/dev/cu.usbmodemXXXX]
set -euo pipefail
cd "$(dirname "$0")/.."
PORT="${1:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "no /dev/cu.usbmodem* port found"; exit 1; }
scripts/build.sh
arduino-cli upload -p "$PORT" --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=no_ota,FlashSize=4M" firmware/espgotchi
