#!/usr/bin/env python3
"""A02b silhouette/optical-depth uniqueness metrics.

Reads the cloud-only view-optical-depth BMPs written by
--debug-mode=view-optical-depth. Each BMP stores OD/(OD+1) in the red
channel. Masks are built at several mapped-OD thresholds; the script
computes IoU, boundary Dice, multi-scale mask correlation, dense-component
statistics and optical-depth-map Pearson correlation against the d0
station of the same variant/scene/seed/height.
"""

import argparse
import csv
import math
import struct
import sys
from pathlib import Path

THRESHOLDS = (0.08, 0.25, 0.45, 0.60, 0.75)
COMPONENT_THRESHOLD = 0.60


def load_bmp_red(path):
    data = path.read_bytes()
    if data[0:2] != b"BM":
        raise ValueError("not a BMP: %s" % path)
    offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 32 or width <= 0 or height == 0:
        raise ValueError("unsupported BMP %dx%d/%d: %s" % (width, height, bpp, path))
    top_down = height < 0
    height = abs(height)
    stride = width * 4
    grid = []
    for y in range(height):
        row = height - 1 - y if not top_down else y
        base = offset + row * stride
        grid.append([data[base + x * 4 + 2] / 255.0 for x in range(width)])
    return grid


def mapped_to_od(value):
    value = min(max(value, 0.0), 0.9995)
    return value / (1.0 - value)


def pearson(a, b):
    n = len(a)
    sa = sum(a)
    sb = sum(b)
    saa = sum(v * v for v in a)
    sbb = sum(v * v for v in b)
    sab = sum(x * y for x, y in zip(a, b))
    num = n * sab - sa * sb
    den = math.sqrt(max(n * saa - sa * sa, 0.0) *
                    max(n * sbb - sb * sb, 0.0))
    return num / den if den > 1e-12 else 0.0


def mask_from(grid, threshold):
    return [[v >= threshold for v in row] for row in grid]


def iou(a, b):
    inter = union = 0
    for ra, rb in zip(a, b):
        for va, vb in zip(ra, rb):
            if va or vb:
                union += 1
                if va and vb:
                    inter += 1
    return inter / union if union else 1.0


def boundary(mask):
    rows = len(mask)
    cols = len(mask[0])
    edge = [[False] * cols for _ in range(rows)]
    for y in range(rows):
        for x in range(cols):
            if not mask[y][x]:
                continue
            if (x == 0 or not mask[y][x - 1] or x == cols - 1 or
                    not mask[y][x + 1] or y == 0 or not mask[y - 1][x] or
                    y == rows - 1 or not mask[y + 1][x]):
                edge[y][x] = True
    return edge


def boundary_dice(a_edge, b_edge):
    rows = len(a_edge)
    cols = len(a_edge[0])
    a_count = sum(sum(row) for row in a_edge)
    b_count = sum(sum(row) for row in b_edge)
    if a_count + b_count == 0:
        return 1.0
    hit = 0
    for y in range(rows):
        for x in range(cols):
            if not b_edge[y][x]:
                continue
            found = False
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < rows and 0 <= nx < cols and a_edge[ny][nx]:
                        found = True
            if found:
                hit += 1
    return 2.0 * hit / (a_count + b_count)


def downsample(mask, factor):
    rows = len(mask)
    cols = len(mask[0])
    out = []
    for y in range(0, rows, factor):
        row = []
        for x in range(0, cols, factor):
            total = 0
            count = 0
            for yy in range(y, min(y + factor, rows)):
                for xx in range(x, min(x + factor, cols)):
                    total += 1 if mask[yy][xx] else 0
                    count += 1
            row.append(total / count)
        out.append(row)
    return [v for row in out for v in row]


def components(mask):
    rows = len(mask)
    cols = len(mask[0])
    seen = [[False] * cols for _ in range(rows)]
    sizes = []
    for y in range(rows):
        for x in range(cols):
            if not mask[y][x] or seen[y][x]:
                continue
            stack = [(y, x)]
            seen[y][x] = True
            size = 0
            while stack:
                cy, cx = stack.pop()
                size += 1
                for ny, nx in ((cy - 1, cx), (cy + 1, cx),
                               (cy, cx - 1), (cy, cx + 1)):
                    if (0 <= ny < rows and 0 <= nx < cols and
                            mask[ny][nx] and not seen[ny][nx]):
                        seen[ny][nx] = True
                        stack.append((ny, nx))
            sizes.append(size)
    sizes.sort(reverse=True)
    occupied = sum(sizes)
    return {
        "count": len(sizes),
        "large_area": sizes[0] if sizes else 0,
        "largest_frac": sizes[0] / occupied if occupied else 0.0,
        "mask_area": occupied,
    }


def largest_component_mask(mask):
    rows = len(mask)
    cols = len(mask[0])
    seen = [[False] * cols for _ in range(rows)]
    best = []
    for y in range(rows):
        for x in range(cols):
            if not mask[y][x] or seen[y][x]:
                continue
            stack = [(y, x)]
            seen[y][x] = True
            comp = []
            while stack:
                cy, cx = stack.pop()
                comp.append((cy, cx))
                for ny, nx in ((cy - 1, cx), (cy + 1, cx),
                               (cy, cx - 1), (cy, cx + 1)):
                    if (0 <= ny < rows and 0 <= nx < cols and
                            mask[ny][nx] and not seen[ny][nx]):
                        seen[ny][nx] = True
                        stack.append((ny, nx))
            if len(comp) > len(best):
                best = comp
    out = [[False] * cols for _ in range(rows)]
    for y, x in best:
        out[y][x] = True
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, default=Path("silhouette_metrics.csv"))
    args = parser.parse_args()

    fields = sorted([p for p in args.root.rglob("clouds_*.bmp")])
    if not fields:
        raise SystemExit("no BMPs under %s" % args.root)
    cache = {}
    rows = []
    for path in fields:
        tag = path.parent.name
        scene = "overcast" if tag.startswith("overcast") else "sunset"
        variant = path.parent.parent.name
        seed = "4242" if "_s4242_" in tag else "1337"
        height = "high" if "_high_" in tag else "low"
        ref_tag = tag.rsplit("_d", 1)[0] + "_d0"
        ref_path = path.parent.parent / ref_tag / path.name
        if not ref_path.is_file():
            continue
        grid = load_bmp_red(path)
        ref = load_bmp_red(ref_path)
        current = [v for row in grid for v in row]
        reference = [v for row in ref for v in row]
        od_corr = pearson(current, reference)
        distance = tag.rsplit("_d", 1)[1]
        for threshold in THRESHOLDS:
            mask = mask_from(grid, threshold)
            ref_mask = mask_from(ref, threshold)
            edge = boundary(mask)
            ref_edge = boundary(ref_mask)
            row = {
                "variant": variant,
                "scene": scene,
                "seed": seed,
                "height": height,
                "distance_m": distance,
                "threshold_mapped_od": threshold,
                "iou": iou(ref_mask, mask),
                "boundary_dice": boundary_dice(ref_edge, edge),
                "corr_4x": pearson(downsample(ref_mask, 4), downsample(mask, 4)),
                "corr_8x": pearson(downsample(ref_mask, 8), downsample(mask, 8)),
                "od_corr": od_corr,
                "components": "",
                "largest_frac": "",
                "largest_iou": "",
            }
            if abs(threshold - COMPONENT_THRESHOLD) < 1e-9:
                rc = components(ref_mask)
                cc = components(mask)
                ref_largest = largest_component_mask(ref_mask)
                cur_largest = largest_component_mask(mask)
                row["components"] = cc["count"]
                row["largest_frac"] = cc["largest_frac"]
                row["largest_iou"] = iou(ref_largest, cur_largest)
            rows.append(row)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        for row in rows:
            writer.writerow({k: (round(v, 6) if isinstance(v, float) else v)
                             for k, v in row.items()})
    print("images=%d rows=%d out=%s" % (len(fields), len(rows), args.out))
    for variant in ("r", "a_w40", "b_a18"):
        for scene in ("sunset", "overcast"):
            selected = [r for r in rows if r["variant"] == variant and
                        r["scene"] == scene and r["seed"] == "1337" and
                        r["height"] == "low" and r["distance_m"] != "0"]
            if not selected:
                continue
            print("%s/%s: IoU %.3f..%.3f ODcorr %.3f..%.3f" % (
                variant, scene,
                min(r["iou"] for r in selected),
                max(r["iou"] for r in selected),
                min(r["od_corr"] for r in selected),
                max(r["od_corr"] for r in selected)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
