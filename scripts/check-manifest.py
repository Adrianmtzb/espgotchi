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

# Verified byte by byte against espgotchi.ino.merged.bin of each board (ESP32-C6 4 MB no_ota,
# ESP32-S3 16 MB app3M_fat9M). Both chips load the bootloader at 0x0, unlike the classic
# ESP32 (0x1000). The names are the ones the release job assembles: <part>-<board>.bin.
BOARDS = {"ESP32-C6": "c6", "ESP32-S3": "s3"}
OFFSETS = {
    "bootloader": 0x0000,
    "partitions": 0x8000,
    "boot_app0":  0xE000,
    "espgotchi":  0x10000,
}


def main() -> int:
    manifest_path = ROOT / "docs" / "manifest.json"
    config_path = ROOT / "firmware" / "espgotchi" / "config.h"

    manifest = json.loads(manifest_path.read_text())
    builds = {b.get("chipFamily"): b for b in manifest["builds"]}
    if set(builds) != set(BOARDS):
        print(f"manifest must have exactly one build per board: {sorted(BOARDS)}", file=sys.stderr)
        return 1
    for family, board in BOARDS.items():
        expected = {f"{part}-{board}.bin": off for part, off in OFFSETS.items()}
        parts = {p["path"]: p["offset"] for p in builds[family]["parts"]}
        if parts != expected:
            print(f"{family}: manifest parts do not match the verified offsets:", file=sys.stderr)
            for name, off in expected.items():
                got = parts.get(name)
                mark = "ok" if got == off else f"got {got}"
                print(f"  {name:20s} expected {off:#07x} ({off})  {mark}", file=sys.stderr)
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

    print(f"manifest ok: {', '.join(BOARDS)}, version {fw_version}, {len(OFFSETS)} parts each")
    return 0


if __name__ == "__main__":
    sys.exit(main())
