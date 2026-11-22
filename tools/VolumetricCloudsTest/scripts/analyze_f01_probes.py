#!/usr/bin/env python3
"""F01 P0 probe consistency analysis.

Compares the DensityVolume and DensityColumns GPU paths:

  * native grids: occupancy / zero-column fractions and grid metadata;
  * shared coordinates (--columns-crosscheck=1): voxel-by-voxel density
    difference, where both paths sample the exact same world position;
  * analytic constant/stripe patterns: same comparison on a known field.

All inputs are Float32; no PNG is used for numbers.
"""

import argparse
import array
import json
import math
import sys
from pathlib import Path

DENSITY_CHANNEL = 3
THRESHOLD = 0.2


def load_volume(directory):
    meta = json.load(open(directory / "density_volume_float32.json",
                          encoding="utf-8"))
    nx, ny, nz = meta["grid"]
    data = array.array("f")
    data.frombytes((directory / "density_volume_float32.bin").read_bytes())
    if len(data) != nx * ny * nz * 4:
        raise ValueError("volume size mismatch: %s" % directory)
    return meta, data[DENSITY_CHANNEL::4], nx, ny, nz


def load_columns(directory):
    meta = json.load(open(directory / "density_columns_float32.json",
                          encoding="utf-8"))
    nx, nz = meta["columns"]
    packets = meta["packets"]
    data = array.array("f")
    data.frombytes((directory / "density_columns_float32.bin").read_bytes())
    if len(data) != nx * packets * nz * 4:
        raise ValueError("columns size mismatch: %s" % directory)
    return meta, data, nx, packets, nz


def volume_columns(data, nx, ny, nz):
    occupied = 0
    zero = 0
    total = nx * nz
    for z in range(nz):
        for x in range(nx):
            top = 0.0
            for y in range(ny):
                top = max(top, data[(z * ny + y) * nx + x])
            if top > THRESHOLD:
                occupied += 1
            if top <= 1e-6:
                zero += 1
    return occupied / total, zero / total


def column_columns(data, nx, packets, nz):
    occupied = 0
    zero = 0
    total = nx * nz
    for z in range(nz):
        for x in range(nx):
            top = 0.0
            for p in range(packets):
                base = ((z * packets + p) * nx + x) * 4
                for c in range(4):
                    top = max(top, data[base + c])
            if top > THRESHOLD:
                occupied += 1
            if top <= 1e-6:
                zero += 1
    return occupied / total, zero / total


def compare_shared(vol, cols, nx, ny, nz):
    # Shared mode requires columns == nx == nz and packets*4 == ny.
    packets = ny // 4
    diffs = []
    mismatched = 0
    threshold_flips = 0
    for z in range(nz):
        for p in range(packets):
            for x in range(nx):
                base = ((z * packets + p) * nx + x) * 4
                for c in range(4):
                    y = p * 4 + c
                    a = vol[(z * ny + y) * nx + x]
                    b = cols[base + c]
                    d = abs(a - b)
                    diffs.append(d)
                    if d > 1e-6:
                        mismatched += 1
                    if (a > THRESHOLD) != (b > THRESHOLD):
                        threshold_flips += 1
    n = len(diffs)
    mean = sum(diffs) / n
    rmse = math.sqrt(sum(d * d for d in diffs) / n)
    # Pearson correlation.
    va = []
    vb = []
    for z in range(nz):
        for p in range(packets):
            for x in range(nx):
                base = ((z * packets + p) * nx + x) * 4
                for c in range(4):
                    y = p * 4 + c
                    va.append(vol[(z * ny + y) * nx + x])
                    vb.append(cols[base + c])
    ma = sum(va) / len(va)
    mb = sum(vb) / len(vb)
    cov = sum((a - ma) * (b - mb) for a, b in zip(va, vb))
    sa = math.sqrt(sum((a - ma) ** 2 for a in va))
    sb = math.sqrt(sum((b - mb) ** 2 for b in vb))
    corr = cov / (sa * sb) if sa > 0 and sb > 0 else 1.0
    return {
        "samples": n,
        "max_abs": max(diffs),
        "mean_abs": mean,
        "rmse": rmse,
        "corr": corr,
        "mismatched_gt_1e6": mismatched,
        "threshold_flips": threshold_flips,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    result = {}
    for directory in sorted(p for p in args.root.iterdir() if p.is_dir()):
        vpath = directory / "density_volume_float32.json"
        cpath = directory / "density_columns_float32.json"
        if not vpath.is_file() or not cpath.is_file():
            continue
        vmeta, vol, nx, ny, nz = load_volume(directory)
        cmeta, cols, cnx, packets, cnz = load_columns(directory)
        v_occ, v_zero = volume_columns(vol, nx, ny, nz)
        c_occ, c_zero = column_columns(cols, cnx, packets, cnz)
        entry = {
            "volume_grid": [nx, ny, nz],
            "columns_grid": [cnx, packets, cnz],
            "volume_origin": vmeta["world_origin"],
            "columns_origin": cmeta["world_origin"],
            "origin_match": vmeta["world_origin"] == cmeta["world_origin"],
            "columns_crosscheck": cmeta.get("columns_crosscheck", 0),
            "volume_permutation": vmeta.get("shader_permutation"),
            "columns_permutation": cmeta.get("shader_permutation"),
            "volume_executable": vmeta["executable_sha256"],
            "columns_executable": cmeta["executable_sha256"],
            "volume_occupied_columns": v_occ,
            "columns_occupied_columns": c_occ,
            "volume_zero_columns": v_zero,
            "columns_zero_columns": c_zero,
            "occupancy_delta_pp": (c_occ - v_occ) * 100.0,
        }
        if (cmeta.get("columns_crosscheck", 0) == 1 and
                cnx == nx and cnz == nz and packets * 4 == ny):
            entry["shared"] = compare_shared(vol, cols, nx, ny, nz)
        result[directory.name] = entry

    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)

    print("%-26s %8s %8s %8s %8s %10s" %
          ("variant", "vol_occ", "col_occ", "delta_pp", "origin", "max_abs"))
    for name, e in result.items():
        shared = e.get("shared")
        max_abs = "%.3e" % shared["max_abs"] if shared else "-"
        print("%-26s %7.2f%% %7.2f%% %+7.2f %8s %10s" % (
            name, e["volume_occupied_columns"] * 100.0,
            e["columns_occupied_columns"] * 100.0, e["occupancy_delta_pp"],
            "ok" if e["origin_match"] else "DIFF", max_abs))
    print("out=%s variants=%d" % (args.out, len(result)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
