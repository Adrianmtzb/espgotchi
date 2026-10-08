#!/usr/bin/env python3
"""Validate the web installer before publishing.

Two things fail silently if nobody looks:

  - The manifest offsets must stay the ones verified against the merged.bin.
    If they drift, esptool writes the parts to the wrong place and the board
    is bricked without a single error message.
  - The version advertised in the manifest must be the one the firmware
    compiles. Otherwise the page offers a version it does not install.

Runs in CI and from `make check`.
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Verified byte by byte against espgotchi.ino.merged.bin (ESP32-C6, 4 MB,
# no_ota partition scheme). The names are the ones the release job assembles.
EXPECTED = {
    "bootloader.bin": 0x0000,
    "partitions.bin": 0x8000,
    "boot_app0.bin":  0xE000,
    "espgotchi.bin":  0x10000,
}
CHIP_FAMILY = "ESP32-C6"


def main() -> int:
    manifest_path = ROOT / "docs" / "manifest.json"
    config_path = ROOT / "firmware" / "espgotchi" / "config.h"

    manifest = json.loads(manifest_path.read_text())
    builds = manifest["builds"]
    if len(builds) != 1 or builds[0].get("chipFamily") != CHIP_FAMILY:
        print(f"manifest must have exactly one build for {CHIP_FAMILY}", file=sys.stderr)
        return 1
    parts = {p["path"]: p["offset"] for p in builds[0]["parts"]}
    if parts != EXPECTED:
        print("manifest parts do not match the verified offsets:", file=sys.stderr)
        for name, off in EXPECTED.items():
            got = parts.get(name)
            mark = "ok" if got == off else f"got {got}"
            print(f"  {name:16s} expected {off:#07x} ({off})  {mark}", file=sys.stderr)
        return 1

    m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', config_path.read_text())
    if not m:
        print("FW_VERSION not found in config.h", file=sys.stderr)
        return 1
    fw_version = m.group(1)
    if manifest.get("version") != fw_version:
        print(
            f"version mismatch: config.h says {fw_version}, manifest says {manifest.get('version')}",
            file=sys.stderr,
        )
        return 1

    print(f"manifest ok: {CHIP_FAMILY}, version {fw_version}, {len(parts)} parts")
    return 0


if __name__ == "__main__":
    sys.exit(main())
