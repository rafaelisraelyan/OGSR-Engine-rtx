#!/usr/bin/env python3
"""Render A03b occupancy-probe horizontal slices.

The probe is a 3D Float32 grid with channels
(macro_raw, local_raw, occupancy, detail_transfer). The renderer takes the
horizontal slice closest to 2000 m and writes one PNG per channel, plus a
JSON with the exact slice provenance.
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
NAMES = ("macro_raw", "local_raw", "occupancy", "detail_transfer")


def color(value):
    value = max(0.0, min(1.0, value))
    for i in range(len(STOPS) - 1):
        a, ca = STOPS[i]
        b, cb = STOPS[i + 1]
        if a <= value <= b:
            t = (value - a) / (b - a) if b > a else 0.0
            return tuple(round(ca[k] + (cb[k] - ca[k]) * t) for k in range(3))
    return STOPS[-1][1]


def chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload +
            struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))


def write_png(path, width, height, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)
        raw.extend(row)
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
    parser.add_argument("--height", type=float, default=2000.0)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for directory in sorted(p for p in args.root.iterdir() if p.is_dir()):
        meta_path = directory / "occupancy_probe_float32.json"
        if not meta_path.is_file():
            continue
        meta = json.load(open(meta_path, encoding="utf-8"))
        nx, ny, nz = meta["grid"]
        cell = meta["cell_size_xz_m"]
        cell_y = meta["cell_size_y_m"]
        origin = meta["world_origin"]
        data = array.array("f")
        data.frombytes((directory / "occupancy_probe_float32.bin").read_bytes())
        if len(data) != nx * ny * nz * 4:
            raise ValueError("size mismatch in %s" % directory)
        iy = max(0, min(ny - 1, round((args.height - origin[1]) / cell_y - 0.5)))
        for channel, name in enumerate(NAMES):
            rows = []
            for z in range(nz):
                row = bytearray()
                for x in range(nx):
                    index = ((z * ny + iy) * nx + x) * 4 + channel
                    row.extend(color(data[index]))
                rows.append(row)
            write_png(args.out / (directory.name + "_" + name + ".png"),
                      nx, nz, rows)
        info = {
            "variant": directory.name,
            "grid": [nx, ny, nz],
            "slice_y_index": iy,
            "slice_y_m": origin[1] + (iy + 0.5) * cell_y,
            "cell_size_xz_m": cell,
            "world_origin": origin,
            "world_extent_m": [nx * cell, nz * cell],
            "channels": list(NAMES),
            "pixel_size_m": cell,
            "aspect": "1:1 world-space, no stretching",
            "macro_seed": meta["macro_seed"],
            "coverage": meta["coverage"],
            "macro_local_occupancy": meta.get("macro_local_occupancy", 0),
        }
        with open(args.out / (directory.name + ".json"), "w",
                  encoding="utf-8") as handle:
            json.dump(info, handle, indent=2)
        print("probe", directory.name, "slice", iy)
    return 0


if __name__ == "__main__":
    sys.exit(main())
