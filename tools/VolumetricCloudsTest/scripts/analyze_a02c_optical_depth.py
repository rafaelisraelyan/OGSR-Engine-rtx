#!/usr/bin/env python3
"""A02c linear optical-depth uniqueness metrics.

Primary source: optical_depth_float32.bin written by
--debug-mode=view-optical-depth. Each pixel stores (mapped_od, od, 0, 1),
where od = -log(cloud_transmittance) before atmosphere/exposure/sRGB.

The analyzer never uses image brightness for the cloud mask. It reports
IoU, boundary Dice, multi-scale mask correlation, connected components and
optical-depth Pearson correlation for several physical OD thresholds.

A separate optional pass compares the same stations decoded from the BMP
capture, both with and without sRGB->linear decoding, to quantify the
A02b diagnostic error.
"""

import argparse
import array
import csv
import json
import math
import os
import struct
import sys
from pathlib import Path

OD_THRESHOLDS = (0.05, 0.10, 0.25, 1.0 / 3.0, 0.50, 1.00, 2.00)
COMPONENT_OD = 1.0 / 3.0
MAPPED_CROSS = 0.25
OD_CROSS = MAPPED_CROSS / (1.0 - MAPPED_CROSS)


def load_float32(path):
    meta_path = path.with_name("optical_depth_float32.json")
    meta = json.load(open(meta_path, encoding="utf-8"))
    width = int(meta["width"])
    height = int(meta["height"])
    data = array.array("f")
    data.frombytes(path.read_bytes())
    pixels = width * height
    channels = int(meta.get("channels", 4))
    if channels == 1:
        if len(data) != pixels:
            raise ValueError("size mismatch: %s" % path)
        od = data
    elif channels == 4:
        if len(data) != pixels * 4:
            raise ValueError("size mismatch: %s" % path)
        od = array.array("f", [0.0]) * pixels
        for i in range(pixels):
            od[i] = data[i * 4 + 1]
    else:
        raise ValueError("unsupported channel count: %s" % path)
    return width, height, od, meta


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
        row = y if top_down else height - 1 - y
        base = offset + row * stride
        grid.append([data[base + x * 4 + 2] / 255.0 for x in range(width)])
    return grid


def srgb_to_linear(value):
    if value <= 0.04045:
        return value / 12.92
    return ((value + 0.055) / 1.055) ** 2.4


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


def mask_from_flat(values, threshold):
    return bytearray(1 if v >= threshold else 0 for v in values)


def iou(a, b):
    inter = 0
    union = 0
    for va, vb in zip(a, b):
        if va or vb:
            union += 1
            if va and vb:
                inter += 1
    return inter / union if union else 1.0


def boundary(mask, width, height):
    edge = bytearray(len(mask))
    for y in range(height):
        base = y * width
        for x in range(width):
            i = base + x
            if not mask[i]:
                continue
            if (x == 0 or not mask[i - 1] or x == width - 1 or
                    not mask[i + 1] or y == 0 or
                    not mask[i - width] or y == height - 1 or
                    not mask[i + width]):
                edge[i] = 1
    return edge


def boundary_dice(a_edge, b_edge):
    a_count = sum(a_edge)
    b_count = sum(b_edge)
    if a_count + b_count == 0:
        return 1.0
    # Dilate b by one pixel and count hits against a. Symmetric enough for
    # station comparisons because both edges use the same rule.
    width = b_width
    height = b_height
    hit = 0
    for y in range(height):
        for x in range(width):
            if not b_edge[y * width + x]:
                continue
            found = False
            for dy in (-1, 0, 1):
                ny = y + dy
                if ny < 0 or ny >= height:
                    continue
                for dx in (-1, 0, 1):
                    nx = x + dx
                    if 0 <= nx < width and a_edge[ny * width + nx]:
                        found = True
                        break
                if found:
                    break
            if found:
                hit += 1
    return 2.0 * hit / (a_count + b_count)


def downsample_mask(mask, width, height, factor):
    rows = []
    for y in range(0, height, factor):
        for x in range(0, width, factor):
            total = 0
            count = 0
            for yy in range(y, min(y + factor, height)):
                base = yy * width
                for xx in range(x, min(x + factor, width)):
                    total += mask[base + xx]
                    count += 1
            rows.append(total / count)
    return rows


def components(mask, width, height):
    seen = bytearray(len(mask))
    sizes = []
    for start in range(len(mask)):
        if not mask[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        size = 0
        while stack:
            i = stack.pop()
            size += 1
            x = i % width
            y = i // width
            if x > 0 and mask[i - 1] and not seen[i - 1]:
                seen[i - 1] = 1
                stack.append(i - 1)
            if x + 1 < width and mask[i + 1] and not seen[i + 1]:
                seen[i + 1] = 1
                stack.append(i + 1)
            if y > 0 and mask[i - width] and not seen[i - width]:
                seen[i - width] = 1
                stack.append(i - width)
            if y + 1 < height and mask[i + width] and not seen[i + width]:
                seen[i + width] = 1
                stack.append(i + width)
        sizes.append(size)
    sizes.sort(reverse=True)
    occupied = sum(sizes)
    return {
        "count": len(sizes),
        "largest_frac": sizes[0] / occupied if occupied else 0.0,
        "largest_area": sizes[0] if sizes else 0,
        "mask_area": occupied,
    }


def largest_component_mask(mask, width, height):
    seen = bytearray(len(mask))
    best = []
    for start in range(len(mask)):
        if not mask[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        comp = []
        while stack:
            i = stack.pop()
            comp.append(i)
            x = i % width
            y = i // width
            if x > 0 and mask[i - 1] and not seen[i - 1]:
                seen[i - 1] = 1
                stack.append(i - 1)
            if x + 1 < width and mask[i + 1] and not seen[i + 1]:
                seen[i + 1] = 1
                stack.append(i + 1)
            if y > 0 and mask[i - width] and not seen[i - width]:
                seen[i - width] = 1
                stack.append(i - width)
            if y + 1 < height and mask[i + width] and not seen[i + width]:
                seen[i + width] = 1
                stack.append(i + width)
        if len(comp) > len(best):
            best = comp
    out = bytearray(len(mask))
    for i in best:
        out[i] = 1
    return out


def parse_tag(tag):
    # <variant>_s<seed>_<height>_d<distance>
    parts = tag.rsplit("_", 3)
    if len(parts) != 4:
        return None
    variant, seed_part, height, dist_part = parts
    if not seed_part.startswith("s") or not dist_part.startswith("d"):
        return None
    try:
        return variant, seed_part[1:], height, int(dist_part[1:])
    except ValueError:
        return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--srgb-out", type=Path, default=None)
    args = parser.parse_args()

    global b_width, b_height
    stations = {}
    for meta_path in sorted(args.root.rglob("optical_depth_float32.json")):
        tag = meta_path.parent.name
        parsed = parse_tag(tag)
        if not parsed:
            continue
        stations[tag] = (meta_path.parent, parsed)

    rows = []
    srgb_rows = []
    for tag, (directory, (variant, seed, height, distance)) in sorted(stations.items()):
        ref_tag = "%s_s%s_%s_d0" % (variant, seed, height)
        if ref_tag not in stations or distance == 0:
            continue
        width, height_px, od, meta = load_float32(
            directory / "optical_depth_float32.bin")
        ref_width, ref_height, ref_od, _ = load_float32(
            stations[ref_tag][0] / "optical_depth_float32.bin")
        if (width, height_px) != (ref_width, ref_height):
            raise ValueError("size mismatch %s vs %s" % (tag, ref_tag))
        b_width, b_height = width, height_px
        corr = pearson(od, ref_od)

        for threshold in OD_THRESHOLDS:
            mask = mask_from_flat(od, threshold)
            ref_mask = mask_from_flat(ref_od, threshold)
            row = {
                "variant": variant,
                "seed": seed,
                "height": height,
                "distance_m": distance,
                "threshold_od": threshold,
                "iou": iou(ref_mask, mask),
                "od_corr": corr,
                "components": "",
                "largest_frac": "",
                "largest_iou": "",
                "boundary_dice": "",
                "corr_4x": "",
                "corr_8x": "",
            }
            if abs(threshold - COMPONENT_OD) < 1e-9:
                edge = boundary(mask, width, height_px)
                ref_edge = boundary(ref_mask, width, height_px)
                row["boundary_dice"] = boundary_dice(ref_edge, edge)
                row["corr_4x"] = pearson(
                    downsample_mask(ref_mask, width, height_px, 4),
                    downsample_mask(mask, width, height_px, 4))
                row["corr_8x"] = pearson(
                    downsample_mask(ref_mask, width, height_px, 8),
                    downsample_mask(mask, width, height_px, 8))
                comp = components(mask, width, height_px)
                row["components"] = comp["count"]
                row["largest_frac"] = comp["largest_frac"]
                row["largest_iou"] = iou(
                    largest_component_mask(ref_mask, width, height_px),
                    largest_component_mask(mask, width, height_px))
            rows.append(row)

        # sRGB / A02b cross-check at the mapped-OD 0.25 threshold.
        bmp_path = directory / "clouds_sunset_horizon.bmp"
        ref_bmp_path = stations[ref_tag][0] / "clouds_sunset_horizon.bmp"
        if args.srgb_out is not None and bmp_path.is_file() and ref_bmp_path.is_file():
            grid = load_bmp_red(bmp_path)
            ref_grid = load_bmp_red(ref_bmp_path)
            flat_encoded = [v for row in grid for v in row]
            ref_encoded = [v for row in ref_grid for v in row]
            flat_linear = [srgb_to_linear(v) for v in flat_encoded]
            ref_linear = [srgb_to_linear(v) for v in ref_encoded]
            float_mask = mask_from_flat(od, OD_CROSS)
            ref_float_mask = mask_from_flat(ref_od, OD_CROSS)
            srgb_rows.append({
                "variant": variant,
                "seed": seed,
                "height": height,
                "distance_m": distance,
                "float32_iou_mapped025": iou(ref_float_mask, float_mask),
                "bmp_srgb_decoded_iou_mapped025": iou(
                    mask_from_flat(ref_linear, MAPPED_CROSS),
                    mask_from_flat(flat_linear, MAPPED_CROSS)),
                "bmp_raw_a02b_iou_mapped025": iou(
                    mask_from_flat(ref_encoded, MAPPED_CROSS),
                    mask_from_flat(flat_encoded, MAPPED_CROSS)),
            })

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        for row in rows:
            writer.writerow({k: (round(v, 6) if isinstance(v, float) else v)
                             for k, v in row.items()})
    print("stations=%d rows=%d out=%s" % (len(stations), len(rows), args.out))

    if args.srgb_out is not None and srgb_rows:
        with args.srgb_out.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(srgb_rows[0].keys()))
            writer.writeheader()
            for row in srgb_rows:
                writer.writerow({k: (round(v, 6) if isinstance(v, float) else v)
                                 for k, v in row.items()})
        print("srgb_rows=%d out=%s" % (len(srgb_rows), args.srgb_out))

    for variant in sorted({r["variant"] for r in rows}):
        selected = [r for r in rows if r["variant"] == variant and
                    r["seed"] == "1337" and r["height"] == "low"]
        if not selected:
            continue
        print("%s: IoU %.3f..%.3f ODcorr %.3f..%.3f" % (
            variant,
            min(r["iou"] for r in selected),
            max(r["iou"] for r in selected),
            min(r["od_corr"] for r in selected),
            max(r["od_corr"] for r in selected)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
