#!/usr/bin/env python3
"""Render corrected world-aspect R01 slices without changing the R01 field.

The original R01 `xy_s*.png` stretched a 16 km x 2.8 km slice to a square.
This script imports the unchanged R01 occupancy evaluator and renders both XY
(z=0) and YZ (x=0) at the correct 16:2.8 aspect ratio.
"""

import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import proto_r01_regions as r01
from r02_diagnostics import (
    color_map,
    png_write,
    resample_nearest,
    rgb_rows_to_buffer,
    world_aspect_size,
)


def render(values, nx, ny, x_extent, y_extent, path, width):
    dst_w, dst_h = world_aspect_size(x_extent, y_extent, width)
    rows = []
    for j in range(ny):
        row = bytearray()
        for i in range(nx):
            row.extend(color_map(values[j * nx + i]))
        rows.append(row)
    buf = resample_nearest(rgb_rows_to_buffer(rows), nx, ny, dst_w, dst_h)
    png_write(path, dst_w, dst_h, buf)
    return dst_w, dst_h


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=1337)
    parser.add_argument("--cut", type=float, default=0.45)
    parser.add_argument("--size", type=float, default=16000.0)
    parser.add_argument("--y-min", type=float, default=1200.0)
    parser.add_argument("--y-max", type=float, default=4000.0)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    nx, ny = 160, 28
    x0 = -args.size / 2.0
    z0 = -args.size / 2.0
    dx = args.size / nx
    dz = args.size / nx
    dy = (args.y_max - args.y_min) / ny
    xy = []
    yz = []
    for iy in range(ny):
        y = args.y_min + (iy + 0.5) * dy
        for ix in range(nx):
            x = x0 + (ix + 0.5) * dx
            xy.append(r01.occupancy(x, y, 0.0, args.seed, args.cut))
        for iz in range(nx):
            z = z0 + (iz + 0.5) * dz
            yz.append(r01.occupancy(0.0, y, z, args.seed, args.cut))
    w1, h1 = render(xy, nx, ny, args.size, args.y_max - args.y_min,
                    args.out / f"r01_xy_corrected_s{args.seed}.png", 800)
    w2, h2 = render(yz, nx, ny, args.size, args.y_max - args.y_min,
                    args.out / f"r01_yz_corrected_s{args.seed}.png", 800)
    meta = {
        "format": "OGSR_V2_3h_A03b_R02_R01_aspect_correction",
        "seed": args.seed,
        "cut": args.cut,
        "world_extent_m": [args.size, args.y_max - args.y_min, args.size],
        "xy_slice_z_m": 0.0,
        "yz_slice_x_m": 0.0,
        "source_grid": [nx, ny],
        "image_size_xy": [w1, h1],
        "image_size_yz": [w2, h2],
        "aspect_rule": "same metres-per-pixel on both axes",
        "r01_evaluator": "proto_r01_regions.py (unchanged)",
    }
    with open(args.out / "r01_aspect_correction.json", "w", encoding="utf-8") as handle:
        json.dump(meta, handle, indent=2)
        handle.write("\n")
    print(json.dumps(meta, indent=2))


if __name__ == "__main__":
    main()
