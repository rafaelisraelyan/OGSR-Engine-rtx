#!/usr/bin/env python3
"""A03 large-mass centroid / component-copy analysis.

Reads the A03 Float32 OD maps (1 or 4 float channels per pixel). For each
station it extracts connected components above a physical OD threshold,
records their area/centroid, and compares the largest masses at a shifted
station with the d0 station. A repeated tile produces matched centroids and
areas; a genuinely different macro layout does not.
"""

import argparse
import array
import csv
import json
import math
import sys
from pathlib import Path

THRESHOLD = 0.333333


def load_od(path):
    meta = json.load(open(path.with_name("optical_depth_float32.json"),
                          encoding="utf-8"))
    data = array.array("f")
    data.frombytes(path.read_bytes())
    w, h = int(meta["width"]), int(meta["height"])
    pixels = w * h
    channels = int(meta.get("channels", 4))
    if channels == 1:
        if len(data) != pixels:
            raise ValueError("size mismatch: %s" % path)
        return w, h, data
    if channels == 4:
        if len(data) != pixels * 4:
            raise ValueError("size mismatch: %s" % path)
        return w, h, data[1::4]
    raise ValueError("unsupported channels: %s" % path)


def components(od, w, h, threshold):
    mask = bytearray(1 if v >= threshold else 0 for v in od)
    seen = bytearray(len(mask))
    out = []
    for start in range(len(mask)):
        if not mask[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        area = 0
        sx = 0.0
        sy = 0.0
        minx = w
        maxx = 0
        miny = h
        maxy = 0
        while stack:
            i = stack.pop()
            x = i % w
            y = i // w
            area += 1
            sx += x
            sy += y
            minx = min(minx, x)
            maxx = max(maxx, x)
            miny = min(miny, y)
            maxy = max(maxy, y)
            if x > 0 and mask[i - 1] and not seen[i - 1]:
                seen[i - 1] = 1
                stack.append(i - 1)
            if x + 1 < w and mask[i + 1] and not seen[i + 1]:
                seen[i + 1] = 1
                stack.append(i + 1)
            if y > 0 and mask[i - w] and not seen[i - w]:
                seen[i - w] = 1
                stack.append(i - w)
            if y + 1 < h and mask[i + w] and not seen[i + w]:
                seen[i + w] = 1
                stack.append(i + w)
        out.append({
            "area": area,
            "cx": sx / area,
            "cy": sy / area,
            "bbox": [minx, miny, maxx, maxy],
        })
    out.sort(key=lambda c: c["area"], reverse=True)
    return out


def parse_tag(tag):
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


def spectrum(od, w, h, max_k=80):
    # Row-mean horizontal profile; descriptive screen-space spectrum.
    profile = [0.0] * w
    for y in range(h):
        base = y * w
        for x in range(w):
            profile[x] += od[base + x]
    inv = 1.0 / h
    profile = [v * inv for v in profile]
    mean = sum(profile) / w
    profile = [(v - mean) * (0.5 - 0.5 * math.cos(2.0 * math.pi * i / (w - 1)))
               for i, v in enumerate(profile)]
    powers = []
    for k in range(1, min(max_k, w // 2) + 1):
        re = 0.0
        im = 0.0
        for i, v in enumerate(profile):
            angle = 2.0 * math.pi * k * i / w
            re += v * math.cos(angle)
            im -= v * math.sin(angle)
        powers.append((k, re * re + im * im))
    if not powers:
        return 0, 0.0, 0.0
    best = max(powers, key=lambda p: p[1])
    total = sum(p[1] for p in powers) or 1.0
    return best[0], best[1] / total, total


def local_maxima(od, w, h, min_od=0.40, block=8, radius=18, limit=24):
    candidates = []
    for by in range(0, h, block):
        for bx in range(0, w, block):
            best = -1.0
            bx_best = bx
            by_best = by
            for y in range(by, min(by + block, h)):
                base = y * w
                for x in range(bx, min(bx + block, w)):
                    value = od[base + x]
                    if value > best:
                        best = value
                        bx_best = x
                        by_best = y
            if best >= min_od:
                candidates.append((best, bx_best, by_best))
    candidates.sort(key=lambda c: c[0], reverse=True)
    selected = []
    for value, x, y in candidates:
        if all((x - sx) ** 2 + (y - sy) ** 2 > radius * radius
               for _, sx, sy in selected):
            selected.append((value, x, y))
        if len(selected) >= limit:
            break
    return selected


def maxima_match(ref, cur, width, height):
    if not ref:
        return 0.0
    diagonal = math.hypot(width, height)
    used = set()
    matched = 0.0
    total = sum(max(v, 1e-6) for v, _, _ in ref)
    for value, x, y in ref:
        best = None
        for index, (v2, x2, y2) in enumerate(cur):
            if index in used:
                continue
            ratio = v2 / max(value, 1e-6)
            if 0.70 <= ratio <= 1.40:
                dist = math.hypot(x2 - x, y2 - y)
                if dist < 0.06 * diagonal and (best is None or dist < best[1]):
                    best = (index, dist)
        if best is not None:
            used.add(best[0])
            matched += max(value, 1e-6)
    return matched / total


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out-csv", type=Path, required=True)
    parser.add_argument("--out-json", type=Path, required=True)
    args = parser.parse_args()

    stations = {}
    for meta in sorted(args.root.rglob("optical_depth_float32.json")):
        tag = meta.parent.name
        parsed = parse_tag(tag)
        if parsed:
            stations[tag] = (meta.parent, parsed)

    rows = []
    summary = {}
    for tag, (directory, (variant, seed, height, distance)) in sorted(stations.items()):
        w, h, od = load_od(directory / "optical_depth_float32.bin")
        comps = components(od, w, h, THRESHOLD)
        k, ratio, total = spectrum(od, w, h)
        rows.append({
            "variant": variant, "seed": seed, "height": height,
            "distance_m": distance, "components": len(comps),
            "largest_area": comps[0]["area"] if comps else 0,
            "spectrum_peak_k": k, "spectrum_peak_ratio": round(ratio, 6),
        })
        for rank, c in enumerate(comps[:10]):
            rows.append({
                "variant": variant, "seed": seed, "height": height,
                "distance_m": distance, "components": "",
                "largest_area": "",
                "spectrum_peak_k": "", "spectrum_peak_ratio": "",
                "rank": rank, "area": c["area"],
                "cx": round(c["cx"], 3), "cy": round(c["cy"], 3),
                "bbox": " ".join(str(v) for v in c["bbox"]),
            })
        # Copy-match against d0 components.
        ref_tag = "%s_s%s_%s_d0" % (variant, seed, height)
        if distance != 0 and ref_tag in stations:
            rw, rh, rod = load_od(stations[ref_tag][0] / "optical_depth_float32.bin")
            ref = components(rod, rw, rh, THRESHOLD)
            ref_maxima = local_maxima(rod, rw, rh)
            cur_maxima = local_maxima(od, w, h)
            diagonal = math.hypot(w, h)
            matched = 0.0
            matched_count = 0
            total_area = sum(c["area"] for c in ref[:10]) or 1
            for c in ref[:10]:
                best = None
                for d in comps:
                    ratio = d["area"] / max(c["area"], 1)
                    if 0.75 <= ratio <= 1.33:
                        dist = math.hypot(d["cx"] - c["cx"], d["cy"] - c["cy"])
                        if best is None or dist < best[1]:
                            best = (d, dist)
                if best is not None and best[1] < 0.05 * diagonal:
                    matched += c["area"]
                    matched_count += 1
            summary.setdefault(variant, {}).setdefault(seed, {})[distance] = {
                "matched_fraction": matched / total_area,
                "matched_count": matched_count,
                "maxima_matched_fraction": maxima_match(
                    ref_maxima, cur_maxima, w, h),
                "ref_maxima": len(ref_maxima),
                "cur_maxima": len(cur_maxima),
                "ref_components": len(ref),
                "cur_components": len(comps),
            }

    args.out_csv.parent.mkdir(parents=True, exist_ok=True)
    keys = ["variant", "seed", "height", "distance_m", "components",
            "largest_area", "spectrum_peak_k", "spectrum_peak_ratio",
            "rank", "area", "cx", "cy", "bbox"]
    with args.out_csv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=keys, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)
    with args.out_json.open("w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2, default=str)
    print("stations=%d rows=%d out=%s" % (len(stations), len(rows), args.out_csv))
    for variant, seeds in summary.items():
        for seed, distances in seeds.items():
            values = [v["matched_fraction"] for v in distances.values()]
            print("%s seed %s matched %.3f..%.3f" % (
                variant, seed, min(values), max(values)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
