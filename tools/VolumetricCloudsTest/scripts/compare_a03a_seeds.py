#!/usr/bin/env python3
"""A03a seed-independence comparisons.

Compares Float32 cloud-only optical-depth maps at identical world positions:

  * Series A: fixed noiseSeed, different macroSeed (macro-only and final);
  * Series B: fixed macroSeed, different noiseSeed (macro-only must be
    independent of the periodic detail texture);
  * same-seed reproducibility (bit identity).

The tool reports pairwise IoU, OD correlation and mean |delta OD|, plus the
period comparison against the variant's own d0 station.
"""

import argparse
import array
import json
import math
import sys
from pathlib import Path

THRESHOLD = 0.333333


def load(path):
    meta = json.load(open(path.with_name("optical_depth_float32.json"),
                          encoding="utf-8"))
    data = array.array("f")
    data.frombytes(path.read_bytes())
    width, height = int(meta["width"]), int(meta["height"])
    channels = int(meta.get("channels", 1))
    if channels == 4:
        data = data[1::4]
    if len(data) != width * height:
        raise ValueError("size mismatch: %s" % path)
    if not all(math.isfinite(v) for v in data):
        raise ValueError("non-finite OD: %s" % path)
    return width, height, data


def masks(a, b, threshold):
    return ([v > threshold for v in a], [v > threshold for v in b])


def iou(ma, mb):
    inter = sum(1 for x, y in zip(ma, mb) if x and y)
    union = sum(1 for x, y in zip(ma, mb) if x or y)
    return inter / union if union else 1.0


def corr(a, b):
    n = len(a)
    ma = sum(a) / n
    mb = sum(b) / n
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((y - mb) ** 2 for y in b)
    if va <= 0.0 or vb <= 0.0:
        return 1.0 if va == vb else 0.0
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(va * vb)


def compare(a, b, threshold):
    ma, mb = masks(a[2], b[2], threshold)
    return {
        "iou": iou(ma, mb),
        "od_corr": corr(a[2], b[2]),
        "mean_abs_od": sum(abs(x - y) for x, y in zip(a[2], b[2])) / len(a[2]),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--threshold", type=float, default=THRESHOLD)
    args = parser.parse_args()

    stations = {}
    for meta in args.root.rglob("optical_depth_float32.json"):
        stations[meta.parent.name] = meta.parent
    loaded = {}
    for name in stations:
        loaded[name] = load(stations[name] / "optical_depth_float32.bin")

    result = {"cross_seed": {}, "period": {}, "reproducibility": {}}
    # Pairwise same-distance comparisons for macro-only and final.
    for distance in (0, 4545):
        for prefix in ("macroonly", "final"):
            names = [n for n in loaded
                     if n.startswith(prefix + "_s") and n.endswith("_low_d%d" % distance)]
            for i in range(len(names)):
                for j in range(i + 1, len(names)):
                    a, b = names[i], names[j]
                    key = "%s | %s" % (a, b)
                    result["cross_seed"][key] = compare(
                        loaded[a], loaded[b], args.threshold)

    # Period comparison for every variant/seed.
    for name in loaded:
        if "_low_d0" not in name:
            continue
        base = name.replace("_low_d0", "")
        for distance in (4545, 9091, 13636, 20000, 40000):
            other = "%s_low_d%d" % (base, distance)
            if other in loaded:
                result["period"]["%s d%d" % (base, distance)] = compare(
                    loaded[name], loaded[other], args.threshold)

    # Reproducibility: rep vs rep2 must be bit-identical.
    if "rep_s1337_low_d0" in loaded and "rep2_s1337_low_d0" in loaded:
        a = loaded["rep_s1337_low_d0"][2]
        b = loaded["rep2_s1337_low_d0"][2]
        result["reproducibility"]["bit_identical"] = (
            a.tobytes() == b.tobytes())
        result["reproducibility"]["mean_abs_od"] = (
            sum(abs(x - y) for x, y in zip(a, b)) / len(a))

    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)

    print("== cross-seed macro-only d0 ==")
    for key, value in result["cross_seed"].items():
        if key.startswith("macroonly") and key.endswith("d0"):
            print("%-42s IoU %.3f OD %.3f" %
                  (key, value["iou"], value["od_corr"]))
    print("== period worst (final) ==")
    worst = sorted(
        ((k, v) for k, v in result["period"].items()
         if k.startswith("final_")),
        key=lambda kv: kv[1]["iou"], reverse=True)[:8]
    for key, value in worst:
        print("%-40s IoU %.3f OD %.3f" %
              (key, value["iou"], value["od_corr"]))
    print("reproducibility", result["reproducibility"])
    print("out=%s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
