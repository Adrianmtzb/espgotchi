#!/usr/bin/env python3
"""Grab the device screen over serial and save it as PNG.

Usage: tools/screenshot.py /dev/cu.usbmodemXXXX out.png
Sends the 'shot' CLI command and decodes the base64 RGB565 framebuffer. No pyserial needed.
"""
import base64, os, struct, subprocess, sys, time, zlib

port, out = sys.argv[1], sys.argv[2]
os.system(f"stty -f {port} 115200 raw -echo")
fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
os.write(fd, b"shot\n")
buf = b""; deadline = time.time() + 20
while b"\nEND" not in buf and time.time() < deadline:
    buf += os.read(fd, 65536)
os.close(fd)
head = buf.index(b"SHOT ")
hdr, _, rest = buf[head:].partition(b"\n")
w, h = map(int, hdr.split()[1:3])
b64 = rest[: rest.index(b"END")].replace(b"\n", b"").replace(b"\r", b"")
raw = base64.b64decode(b64)
assert len(raw) >= w * h * 2, (len(raw), w, h)
rows = []
for y in range(h):
    row = bytearray([0])
    for x in range(w):
        lo, hi = raw[(y * w + x) * 2], raw[(y * w + x) * 2 + 1]  # framebuffer is native little-endian uint16
        v = hi << 8 | lo
        row += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
    rows.append(bytes(row))
def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
open(out, "wb").write(png)
print(f"{out}: {w}x{h}")
