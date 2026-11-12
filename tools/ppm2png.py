#!/usr/bin/env python3
"""Turns the preview tool's .ppm pictures into .png files (no dependencies).

    python3 tools/ppm2png.py <folder or file> [...]

Each P6 .ppm becomes a .png beside it (the .ppm is removed).
"""
import os
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # header: P6 <w> <h> <max>, whitespace-separated, then one whitespace byte
    tokens, pos = [], 0
    while len(tokens) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        tokens.append(data[pos:end])
        pos = end
    if tokens[0] != b"P6" or tokens[3] != b"255":
        raise ValueError(f"{path}: not an 8-bit P6 PPM")
    w, h = int(tokens[1]), int(tokens[2])
    pixels = data[pos + 1:pos + 1 + w * h * 3]
    if len(pixels) != w * h * 3:
        raise ValueError(f"{path}: truncated")
    return w, h, pixels


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def write_png(path, w, h, pixels):
    stride = w * 3
    raw = bytearray()
    prev = bytes(stride)
    for y in range(h):
        row = pixels[y * stride:(y + 1) * stride]
        # filter 2 (Up): flat areas compress well
        raw.append(2)
        raw.extend((a - b) & 0xFF for a, b in zip(row, prev))
        prev = row
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def main(args):
    if not args:
        print(__doc__)
        return 2
    files = []
    for a in args:
        if os.path.isdir(a):
            files += sorted(os.path.join(a, n) for n in os.listdir(a) if n.endswith(".ppm"))
        else:
            files.append(a)
    for path in files:
        w, h, pixels = read_ppm(path)
        out = os.path.splitext(path)[0] + ".png"
        write_png(out, w, h, pixels)
        os.remove(path)
        print(f"{out}  {w}x{h}  {os.path.getsize(out) // 1024} KB")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
