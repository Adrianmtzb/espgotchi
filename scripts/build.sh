#!/usr/bin/env bash
# Regenerates embedded assets and compiles the firmware.
set -euo pipefail
cd "$(dirname "$0")/.."
node tools/gen_sprites.mjs
node tools/gen_web.mjs
arduino-cli compile --fqbn "esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=no_ota,FlashSize=4M" firmware/espgotchi "$@"
