#!/usr/bin/env python3
"""Render one horizontal macro-probe layer from a 3D Float32 probe."""

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

    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                               8, 2, 0, 0, 0)))
        handle.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        handle.write(chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--world-y", type=float, default=900.0)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    meta = json.load(open(args.directory / "macro_probe_float32.json",
                          encoding="utf-8"))
    nx, ny, nz = meta["grid"]
    cell_y = float(meta["cell_size_y_m"])
    origin_y = float(meta["world_origin"][1])
    layer = int((args.world_y - origin_y) / cell_y)
    layer = max(0, min(ny - 1, layer))
    data = array.array("f")
    data.frombytes((args.directory / "macro_probe_float32.bin").read_bytes())
    if len(data) != nx * ny * nz * 4:
        raise ValueError("size mismatch")
    args.out.mkdir(parents=True, exist_ok=True)
    for channel, name in enumerate(NAMES):
        rows = []
        for z in range(nz):
            row = bytearray()
            for x in range(nx):
                row.extend(color(data[((z * ny + layer) * nx + x) * 4 +
                                      channel]))
            rows.append(row)
        write_png(args.out / (args.directory.name + "_" + name + ".png"),
                  nx, nz, rows)
    info = {
        "source": str(args.directory),
        "layer_index": layer,
        "world_y_m": origin_y + (layer + 0.5) * cell_y,
        "cell_size_xz_m": meta["cell_size_xz_m"],
        "world_origin": meta["world_origin"],
        "channels": list(NAMES),
    }
    with open(args.out / (args.directory.name + ".json"), "w",
              encoding="utf-8") as handle:
        json.dump(info, handle, indent=2)
    print("layer %d (%.1f m) -> %s" % (layer, info["world_y_m"], args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
