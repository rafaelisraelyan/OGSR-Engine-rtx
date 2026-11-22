#!/usr/bin/env python3
"""Render the A03 60 km horizontal slice (gridY == 1).

The dump is 512 x 1 x 512 Float32 records: raw_shape, shaped, envelope,
density. raw_shape is the macro-only mass when --macro-detail=0; density is
the final renderer value. One pixel = one 117.1875 m cell; no stretching.
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
        meta_path = directory / "density_volume_float32.json"
        if not meta_path.is_file():
            continue
        meta = json.load(open(meta_path, encoding="utf-8"))
        nx, ny, nz = meta["grid"]
        if ny != 1:
            continue
        data = array.array("f")
        data.frombytes((directory / "density_volume_float32.bin").read_bytes())
        if len(data) != nx * nz * 4:
            raise ValueError("size mismatch in %s" % directory)
        for channel, name in ((0, "raw_shape"), (3, "density")):
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
            "channels": {"raw_shape": 0, "density": 3},
            "pixel_size_m": meta["cell_size_xz_m"],
            "aspect": "1:1 world-space, no stretching",
        }
        with open(args.out / (directory.name + ".json"), "w",
                  encoding="utf-8") as handle:
            json.dump(info, handle, indent=2)
        print("slice", directory.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
