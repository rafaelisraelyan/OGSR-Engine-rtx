#!/usr/bin/env python3
"""Render aspect-correct world-space slices from the A02c Float32 volume.

No third-party modules: a minimal RGB8 PNG writer is included. Density is
shown on a fixed 0..1 scale so S0/S1/S2old/S2new are directly comparable.
XY/YZ slices are not squared: one pixel is one voxel, then a uniform integer
upscale keeps the 16 km x 4.3 km aspect ratio.
"""

import argparse
import array
import json
import os
import struct
import sys
import zlib
from pathlib import Path

STOPS = (
    (0.00, (4, 6, 18)),
    (0.05, (10, 30, 80)),
    (0.15, (0, 110, 200)),
    (0.30, (0, 210, 190)),
    (0.50, (190, 240, 60)),
    (0.70, (255, 190, 0)),
    (0.90, (240, 60, 20)),
    (1.00, (255, 245, 235)),
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
    compressed = zlib.compress(bytes(raw), 9)

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", ihdr))
        handle.write(chunk(b"IDAT", compressed))
        handle.write(chunk(b"IEND", b""))


def render_density_plane(values, width, height, scale):
    rows = []
    for y in range(height):
        row = bytearray()
        for x in range(width):
            rgb = color(values[y * width + x])
            row.extend(rgb * scale)
        for _ in range(scale):
            rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--scale", type=int, default=4)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    for directory in sorted(p for p in args.root.iterdir() if p.is_dir()):
        meta_path = directory / "density_volume_float32.json"
        if not meta_path.is_file():
            continue
        meta = json.load(open(meta_path, encoding="utf-8"))
        nx, ny, nz = meta["grid"]
        cxz = meta["cell_size_xz_m"]
        cy = meta["cell_size_y_m"]
        ox, oy, oz = meta["world_origin"]
        volume = array.array("f")
        volume.frombytes((directory / "density_volume_float32.bin").read_bytes())
        name = directory.name
        out = args.out / name
        out.mkdir(parents=True, exist_ok=True)

        def density(x, y, z):
            return volume[((z * ny + y) * nx + x) * 4 + 3]

        # Horizontal XZ slices at fixed world heights.
        for y_m in (800.0, 1600.0, 2400.0, 3200.0):
            y = min(ny - 1, max(0, int((y_m - oy) / cy)))
            values = [density(x, y, z) for z in range(nz) for x in range(nx)]
            rows = render_density_plane(values, nx, nz, args.scale)
            write_png(out / ("xz_y%04d.png" % int(y_m)), nx * args.scale,
                      nz * args.scale, rows)

        # Vertical XY slice at the world Z centre.
        z = nz // 2
        values = [density(x, y, z) for y in range(ny) for x in range(nx)]
        rows = render_density_plane(values, nx, ny, args.scale)
        write_png(out / "xy_z_center.png", nx * args.scale, ny * args.scale,
                  rows)

        # Vertical YZ slice at the world X centre.
        x = nx // 2
        values = [density(x, y, z) for y in range(ny) for z in range(nz)]
        rows = render_density_plane(values, nz, ny, args.scale)
        write_png(out / "yz_x_center.png", nz * args.scale, ny * args.scale,
                  rows)

        # Top-down maximum density over Y.
        values = []
        for z in range(nz):
            for x in range(nx):
                values.append(max(density(x, y, z) for y in range(ny)))
        rows = render_density_plane(values, nx, nz, args.scale)
        write_png(out / "xz_max_projection.png", nx * args.scale,
                  nz * args.scale, rows)

        # Cloud top height (highest voxel with density > 0.2).
        tops = []
        for z in range(nz):
            for x in range(nx):
                top = 0
                for y in range(ny - 1, -1, -1):
                    if density(x, y, z) > 0.2:
                        top = y
                        break
                tops.append(top / max(1, ny - 1))
        rows = render_density_plane(tops, nx, nz, args.scale)
        write_png(out / "cloud_top_height.png", nx * args.scale,
                  nz * args.scale, rows)

        info = {
            "variant": name,
            "grid": [nx, ny, nz],
            "cell_size_xz_m": cxz,
            "cell_size_y_m": cy,
            "world_origin": [ox, oy, oz],
            "world_extent_m": [nx * cxz, ny * cy, nz * cxz],
            "xz_slice_heights_m": [800.0, 1600.0, 2400.0, 3200.0],
            "color_scale": "fixed density 0..1 (dark blue -> white)",
            "top_height_scale": "0..1 of grid Y; multiply by world_extent Y",
            "xy_yz_aspect": "one pixel per voxel, uniform upscale; not squared",
            "scale": args.scale,
        }
        with open(out / "slices.json", "w", encoding="utf-8") as handle:
            json.dump(info, handle, indent=2)
        print("slices", name)

    return 0


if __name__ == "__main__":
    sys.exit(main())
