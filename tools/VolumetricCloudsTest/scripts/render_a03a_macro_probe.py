#!/usr/bin/env python3
"""Render A03a macro-probe Float32 slices.

The macro probe has four channels: macro_raw, macro_mass, detail_transfer,
macro_mass_covered. Each is rendered with an explicit world aspect (one pixel
per cell, no stretching). A metadata JSON records origin, cell and channel.
"""

import argparse
import array
import json
import struct
import sys
import zlib
from pathlib import Path

STOPS = (
    (0.00, (4, 6, 18)), (0.05, (10, 30, 80)), (0.15, (0, 110, 200)),
    (0.30, (0, 210, 190)), (0.50, (190, 240, 60)), (0.70, (255, 190, 0)),
    (0.90, (240, 60, 20)), (1.00, (255, 245, 235)),
)
NAMES = ("macro_raw", "macro_mass", "detail_transfer", "macro_mass_covered")


def color(value):
    value = max(0.0, min(1.0, value))
    for i in range(len(STOPS) - 1):
        a, ca = STOPS[i]
        b, cb = STOPS[i + 1]
        if a <= value <= b:
            t = (value - a) / (b - a) if b > a else 0.0
            return tuple(round(ca[k] + (cb[k] - ca[k]) * t) for k in range(3))
    return STOPS[-1][1]


def write_png(path, width, height, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)
        raw.extend(row)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", ihdr))
        handle.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        handle.write(chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for directory in sorted(p for p in args.root.iterdir() if p.is_dir()):
        meta_path = directory / "macro_probe_float32.json"
        if not meta_path.is_file():
            continue
        meta = json.load(open(meta_path, encoding="utf-8"))
        nx, ny, nz = meta["grid"]
        if ny != 1:
            continue
        data = array.array("f")
        data.frombytes((directory / "macro_probe_float32.bin").read_bytes())
        if len(data) != nx * nz * 4:
            raise ValueError("size mismatch in %s" % directory)
        for channel, name in enumerate(NAMES):
            rows = []
            for z in range(nz):
                row = bytearray()
                for x in range(nx):
                    row.extend(color(data[(z * nx + x) * 4 + channel]))
                rows.append(row)
            write_png(args.out / (directory.name + "_" + name + ".png"),
                      nx, nz, rows)
        info = {
            "variant": directory.name,
            "grid": [nx, ny, nz],
            "cell_size_xz_m": meta["cell_size_xz_m"],
            "world_origin": meta["world_origin"],
            "world_extent_m": [nx * meta["cell_size_xz_m"],
                               nz * meta["cell_size_xz_m"]],
            "channels": list(NAMES),
            "pixel_size_m": meta["cell_size_xz_m"],
            "aspect": "1:1 world-space, no stretching",
            "macro_seed": meta["macro_seed"],
            "coverage": meta["coverage"],
            "coverage_preserve": meta["macro_coverage_preserve"],
            "seed_hash": meta["macro_seed_hash"],
        }
        with open(args.out / (directory.name + ".json"), "w",
                  encoding="utf-8") as handle:
            json.dump(info, handle, indent=2)
        print("probe", directory.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
