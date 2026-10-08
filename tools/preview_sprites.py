#!/usr/bin/env python3
"""Validates shared/sprites.json and renders a PNG contact sheet (needs macOS `sips`)."""
import json, os, subprocess, sys
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
src = json.load(open(os.path.join(root, "shared/sprites.json")))
LEGACY = {"#": 0, "o": 1, "x": 2}

def resolve(name, spr, all_):
    rows = spr.get("rows")
    if rows is None:
        base = all_[spr["base"]]
        rows = list(resolve(spr["base"], base, all_)[0])
        for k, v in spr["patch"].items():
            rows[int(k)] = v
    pal = spr.get("palette") or all_[spr["base"]]["palette"]
    return rows, pal

errors, out = [], {}
for name, spr in src["sprites"].items():
    rows, pal = resolve(name, spr, src["sprites"])
    n = len(rows)
    for i, r in enumerate(rows):
        if len(r) != n:
            errors.append(f"{name}: row {i} has {len(r)} chars (expected {n}): {r}")
        for c in r:
            if c == ".": continue
            idx = LEGACY.get(c, int(c) if c.isdigit() else -1)
            if idx < 0 or idx >= len(pal):
                errors.append(f"{name}: row {i} bad char {c!r} for palette of {len(pal)}")
    out[name] = (rows, pal)
if errors:
    print("\n".join(errors)); sys.exit(1)

# contact sheet
scale, pad, cols = 3, 6, 8
cell = 24 * scale + pad
names = list(out)
W, H = cols * cell + pad, ((len(names) + cols - 1) // cols) * cell + pad
img = bytearray(b"\x40\x44\x52" * (W * H))
def put(x, y, rgb):
    if 0 <= x < W and 0 <= y < H:
        i = (y * W + x) * 3; img[i:i+3] = rgb
for k, name in enumerate(names):
    rows, pal = out[name]
    ox, oy = pad + (k % cols) * cell, pad + (k // cols) * cell
    n = len(rows); s = scale * 24 // n
    for r, row in enumerate(rows):
        for c, ch in enumerate(row):
            if ch == ".": continue
            idx = LEGACY.get(ch, int(ch) if ch.isdigit() else 0)
            h = pal[idx].lstrip("#"); rgb = bytes(int(h[i:i+2], 16) for i in (0, 2, 4))
            for dy in range(s):
                for dx in range(s):
                    put(ox + c * s + dx, oy + r * s + dy, rgb)
dest = sys.argv[1] if len(sys.argv) > 1 else "/tmp/sprites.ppm"
with open(dest, "wb") as f:
    f.write(f"P6 {W} {H} 255\n".encode()); f.write(img)
png = dest.rsplit(".", 1)[0] + ".png"
subprocess.run(["sips", "-s", "format", "png", dest, "--out", png], capture_output=True)
print(f"{len(names)} sprites OK -> {png}")
