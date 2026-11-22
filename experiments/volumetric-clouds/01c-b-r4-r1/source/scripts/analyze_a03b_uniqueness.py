#!/usr/bin/env python3
"""A03b uniqueness: 3D density and occupancy-probe station comparison."""

import argparse
import array
import json
import math
import sys
from pathlib import Path


def load(directory, name, channels):
    meta = json.load(open(directory / (name + ".json"), encoding="utf-8"))
    data = array.array("f")
    data.frombytes((directory / (name + ".bin")).read_bytes())
    nx, ny, nz = meta["grid"]
    if len(data) != nx * ny * nz * channels:
        raise ValueError("size mismatch: %s" % directory)
    return meta, data


def pearson(a, b):
    n = len(a)
    ma = sum(a) / n
    mb = sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((x - mb) ** 2 for x in b)
    if va <= 0 or vb <= 0:
        return 1.0
    cov = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    return cov / math.sqrt(va * vb)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = {}
    for d0 in sorted(p for p in args.root.iterdir() if p.is_dir()):
        if not d0.name.endswith("_d0"):
            continue
        stem = d0.name[:-3]
        for child in sorted(p for p in args.root.iterdir() if p.is_dir()):
            if not child.name.startswith(stem + "_d") or child.name == d0.name:
                continue
            distance = int(child.name.rsplit("_d", 1)[1])
            entry = {"distance_m": distance}
            try:
                m0, v0 = load(d0, "density_volume_float32", 4)
                m1, v1 = load(child, "density_volume_float32", 4)
                if m0["grid"] != m1["grid"]:
                    continue
                a = list(v0[3::4])
                b = list(v1[3::4])
                entry["density_corr"] = pearson(a, b)
                entry["density_max_abs"] = max(abs(x - y) for x, y in zip(a, b))
                mask_a = [x > 0.2 for x in a]
                mask_b = [x > 0.2 for x in b]
                inter = sum(1 for x, y in zip(mask_a, mask_b) if x and y)
                union = sum(1 for x, y in zip(mask_a, mask_b) if x or y)
                entry["voxel_iou_02"] = inter / union if union else 1.0
            except FileNotFoundError:
                pass
            try:
                m0, p0 = load(d0, "occupancy_probe_float32", 4)
                m1, p1 = load(child, "occupancy_probe_float32", 4)
                a = list(p0[2::4])
                b = list(p1[2::4])
                entry["occupancy_corr"] = pearson(a, b)
                entry["occupancy_max_abs"] = max(abs(x - y) for x, y in zip(a, b))
                ma = [x > 0.2 for x in a]
                mb = [x > 0.2 for x in b]
                inter = sum(1 for x, y in zip(ma, mb) if x and y)
                union = sum(1 for x, y in zip(ma, mb) if x or y)
                entry["occupancy_iou_02"] = inter / union if union else 1.0
            except FileNotFoundError:
                pass
            result[stem] = result.get(stem, {})
            result[stem][str(distance)] = entry
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    print("out=%s groups=%d" % (args.out, len(result)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
