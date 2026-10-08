#!/usr/bin/env node
// Renders the social card (og:image) for the landing page: 1200x630 PNG, no dependencies.
// The title is set in a 5x7 pixel font and the nine adult pets sit on their room colors, so the
// card looks like the firmware does. Usage: node tools/gen_og.mjs [out.png]
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { deflateSync } from "node:zlib";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const out = process.argv[2] || join(root, "_site/og.png");
const sheet = JSON.parse(readFileSync(join(root, "shared/sprites.json"), "utf8"));
const manifest = JSON.parse(readFileSync(join(root, "docs/manifest.json"), "utf8"));

const W = 1200, H = 630;
const px = new Uint8Array(W * H * 3);
const hex = (h) => [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)];
function rect(x, y, w, h, color) {
  const [r, g, b] = typeof color === "string" ? hex(color) : color;
  for (let yy = Math.max(0, y); yy < Math.min(H, y + h); yy++)
    for (let xx = Math.max(0, x); xx < Math.min(W, x + w); xx++) {
      const i = (yy * W + xx) * 3; px[i] = r; px[i + 1] = g; px[i + 2] = b;
    }
}
function roundRect(x, y, w, h, rad, color) {
  rect(x + rad, y, w - 2 * rad, h, color); rect(x, y + rad, w, h - 2 * rad, color);
  for (const [cx, cy] of [[x + rad, y + rad], [x + w - rad - 1, y + rad], [x + rad, y + h - rad - 1], [x + w - rad - 1, y + h - rad - 1]])
    for (let yy = -rad; yy <= rad; yy++) for (let xx = -rad; xx <= rad; xx++) if (xx * xx + yy * yy <= rad * rad) rect(cx + xx, cy + yy, 1, 1, color);
}

// 5x7 pixel font on an 8-row cell so g j p q y keep their descenders. '1' = ink.
const FONT = {
  A: ["01110", "10001", "10001", "11111", "10001", "10001", "10001"], B: ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
  C: ["01110", "10001", "10000", "10000", "10000", "10001", "01110"], D: ["11100", "10010", "10001", "10001", "10001", "10010", "11100"],
  E: ["11111", "10000", "10000", "11110", "10000", "10000", "11111"], F: ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
  G: ["01110", "10001", "10000", "10111", "10001", "10001", "01111"], H: ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
  I: ["01110", "00100", "00100", "00100", "00100", "00100", "01110"], J: ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
  K: ["10001", "10010", "10100", "11000", "10100", "10010", "10001"], L: ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
  M: ["10001", "11011", "10101", "10101", "10001", "10001", "10001"], N: ["10001", "10001", "11001", "10101", "10011", "10001", "10001"],
  O: ["01110", "10001", "10001", "10001", "10001", "10001", "01110"], P: ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
  Q: ["01110", "10001", "10001", "10001", "10101", "10010", "01101"], R: ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
  S: ["01111", "10000", "10000", "01110", "00001", "00001", "11110"], T: ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
  U: ["10001", "10001", "10001", "10001", "10001", "10001", "01110"], V: ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
  W: ["10001", "10001", "10001", "10101", "10101", "10101", "01010"], X: ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
  Y: ["10001", "10001", "10001", "01010", "00100", "00100", "00100"], Z: ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
  a: ["00000", "00000", "01110", "00001", "01111", "10001", "01111"], b: ["10000", "10000", "10110", "11001", "10001", "10001", "11110"],
  c: ["00000", "00000", "01110", "10000", "10000", "10001", "01110"], d: ["00001", "00001", "01101", "10011", "10001", "10001", "01111"],
  e: ["00000", "00000", "01110", "10001", "11111", "10000", "01110"], f: ["00110", "01001", "01000", "11100", "01000", "01000", "01000"],
  g: ["00000", "00000", "01111", "10001", "10001", "01111", "00001", "01110"], h: ["10000", "10000", "10110", "11001", "10001", "10001", "10001"],
  i: ["00100", "00000", "01100", "00100", "00100", "00100", "01110"], j: ["00010", "00000", "00110", "00010", "00010", "00010", "10010", "01100"],
  k: ["10000", "10000", "10010", "10100", "11000", "10100", "10010"], l: ["01100", "00100", "00100", "00100", "00100", "00100", "01110"],
  m: ["00000", "00000", "11010", "10101", "10101", "10001", "10001"], n: ["00000", "00000", "10110", "11001", "10001", "10001", "10001"],
  o: ["00000", "00000", "01110", "10001", "10001", "10001", "01110"], p: ["00000", "00000", "11110", "10001", "10001", "11110", "10000", "10000"],
  q: ["00000", "00000", "01111", "10001", "10001", "01111", "00001", "00001"], r: ["00000", "00000", "10110", "11001", "10000", "10000", "10000"],
  s: ["00000", "00000", "01110", "10000", "01110", "00001", "11110"], t: ["01000", "01000", "11100", "01000", "01000", "01001", "00110"],
  u: ["00000", "00000", "10001", "10001", "10001", "10011", "01101"], v: ["00000", "00000", "10001", "10001", "10001", "01010", "00100"],
  w: ["00000", "00000", "10001", "10001", "10101", "10101", "01010"], x: ["00000", "00000", "10001", "01010", "00100", "01010", "10001"],
  y: ["00000", "00000", "10001", "10001", "10001", "01111", "00001", "01110"], z: ["00000", "00000", "11111", "00010", "00100", "01000", "11111"],
  0: ["01110", "10001", "10011", "10101", "11001", "10001", "01110"], 1: ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
  2: ["01110", "10001", "00001", "00010", "00100", "01000", "11111"], 3: ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
  4: ["00010", "00110", "01010", "10010", "11111", "00010", "00010"], 5: ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
  6: ["00110", "01000", "10000", "11110", "10001", "10001", "01110"], 7: ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
  8: ["01110", "10001", "10001", "01110", "10001", "10001", "01110"], 9: ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
  " ": ["00000", "00000", "00000", "00000", "00000", "00000", "00000"], ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
  ",": ["00000", "00000", "00000", "00000", "01100", "00100", "01000"], "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
  "·": ["00000", "00000", "00000", "01100", "01100", "00000", "00000"], "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
  ":": ["00000", "01100", "01100", "00000", "01100", "01100", "00000"], "$": ["00100", "01111", "10100", "01110", "00101", "11110", "00100"],
  "'": ["01100", "00100", "01000", "00000", "00000", "00000", "00000"], "!": ["00100", "00100", "00100", "00100", "00100", "00000", "00100"],
  "&": ["01000", "10100", "10100", "01000", "10101", "10010", "01101"],
};
function text(x, y, s, scale, color) {
  for (const ch of s) {
    const g = FONT[ch] || FONT[" "];
    g.forEach((row, r) => [...row].forEach((b, c) => { if (b === "1") rect(x + c * scale, y + r * scale, scale, scale, color); }));
    x += 6 * scale;
  }
  return x;
}
const textWidth = (s, scale) => s.length * 6 * scale - scale;

function resolve(name) {
  const s = sheet.sprites[name];
  if (s.rows) return s;
  const base = resolve(s.base);
  const rows = [...base.rows];
  for (const [k, v] of Object.entries(s.patch || {})) rows[+k] = v;
  return { rows, palette: s.palette || base.palette };
}
const PAL = { "#": 0, o: 1, x: 2 };
function sprite(name, x, y, scale) {
  const s = resolve(name);
  s.rows.forEach((row, r) => [...row].forEach((ch, c) => {
    if (ch === ".") return;
    rect(x + c * scale, y + r * scale, scale, scale, s.palette[ch in PAL ? PAL[ch] : +ch]);
  }));
}

// ---- composition ----
const BG = "#0b0d16", FG = "#f1efe8", DIM = "#9aa0b8", ACCENT = "#ff5c8a";
rect(0, 0, W, H, BG);
sprite("heart", 64, 78, 5);
text(160, 76, "ESPgotchi", 10, FG);
rect(64, 164, 90, 6, ACCENT);
text(64, 190, "A pixel pet that lives on a $15 ESP32 board", 3, DIM);

// the nine species, adult frame on the species' own room, like the screen
const keys = Object.keys(sheet.species);
const tile = 116, margin = 40, gap = (W - 2 * margin - keys.length * tile) / (keys.length - 1);
keys.forEach((key, i) => {
  const t = sheet.species[key], x = Math.round(margin + i * (tile + gap)), y = 272;
  roundRect(x, y, tile, tile + 26, 14, t.bg);
  rect(x + 6, y + tile - 30, tile - 12, 30, t.bg2);
  // shadow, then the pet
  rect(x + 24, y + tile - 24, tile - 48, 4, t.bg2);
  sprite(`${key}_adult_0`, x + (tile - 96) / 2, y + tile - 96 - 20, 4);
  const label = textWidth(t.label, 2) > tile - 8 ? key[0].toUpperCase() + key.slice(1) : t.label;  // "Edgerunner" does not fit
  text(x + Math.round((tile - textWidth(label, 2)) / 2), y + tile + 6, label, 2, "#1a1a2e");
});

text(64, 528, "Nine species, five stages, three adult forms. Open source, MIT.", 3, DIM);
text(64, 572, `v${manifest.version} · adrianmtzb.github.io/espgotchi · by adrianmb`, 3, ACCENT);

// ---- PNG encoder (RGB, 8-bit, no filter) ----
const crcTable = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
const crc = (buf) => { let c = -1; for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8); return (c ^ -1) >>> 0; };
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, "ascii"), data]);
  const c = Buffer.alloc(4); c.writeUInt32BE(crc(td));
  return Buffer.concat([len, td, c]);
}
const raw = Buffer.alloc((W * 3 + 1) * H);
for (let y = 0; y < H; y++) { raw[y * (W * 3 + 1)] = 0; Buffer.from(px.buffer, y * W * 3, W * 3).copy(raw, y * (W * 3 + 1) + 1); }
const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
const png = Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]), chunk("IHDR", ihdr), chunk("IDAT", deflateSync(raw, { level: 9 })), chunk("IEND", Buffer.alloc(0))]);
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, png);
console.log(`${out}: ${W}x${H}, ${png.length} bytes`);
