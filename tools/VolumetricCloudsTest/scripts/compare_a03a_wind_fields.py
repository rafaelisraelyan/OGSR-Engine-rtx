#!/usr/bin/env python3
"""Compare two Float32 volume dumps channel by channel."""

import argparse
import array
import json
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--left-name", default="density_volume_float32")
    parser.add_argument("--right-name", default="density_volume_float32")
    args = parser.parse_args()

    result = {}
    for name, left_name, right_name in (
        ("density", args.left_name, args.right_name),
        ("macro", "macro_probe_float32", "macro_probe_float32"),
    ):
        left_path = args.left / (left_name + ".json")
        right_path = args.right / (right_name + ".json")
        if not left_path.is_file() or not right_path.is_file():
            continue
        lmeta, left = load(args.left, left_name, 4)
        rmeta, right = load(args.right, right_name, 4)
        if lmeta["grid"] != rmeta["grid"]:
            raise ValueError("grid mismatch")
        channels = 4
        diffs = []
        for c in range(channels):
            values = [abs(a - b) for a, b in
                      zip(left[c::channels], right[c::channels])]
            diffs.append({
                "channel": c,
                "max_abs": max(values),
                "mean_abs": sum(values) / len(values),
                "nonzero": sum(1 for v in values if v > 0.0),
            })
        result[name] = {
            "grid": lmeta["grid"],
            "left": str(args.left),
            "right": str(args.right),
            "channels": diffs,
        }

    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    for name, entry in result.items():
        print("%s:" % name)
        for c in entry["channels"]:
            print("  ch%d max %.3e mean %.3e nonzero %d" % (
                c["channel"], c["max_abs"], c["mean_abs"], c["nonzero"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
