#!/usr/bin/env python3
"""A03 flight continuity analysis.

Parses the deterministic level-flight metrics and reports the frame-to-frame
difference at the old NoiseVolume period (4545.4545 m) and at the new 6 km
macro lattice boundary. No screenshots are used for the temporal verdict.
"""

import argparse
import json
import math
import os
import re
import sys
from pathlib import Path

PERIOD = 4545.454545454545
MACRO = 6000.0
LINE = re.compile(
    r"frame (\d+) hf ([0-9.]+) diff ([0-9.]+) camera "
    r"([-0-9.]+) ([-0-9.]+) ([-0-9.]+) yaw ([-0-9.]+)")


def frame_for_distance(start, axis_sign, distance, step):
    if abs(axis_sign) < 1e-6:
        return None
    return (distance - start) / (axis_sign * step)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    result = {}
    for directory in sorted(p for p in args.root.iterdir() if p.is_dir()):
        metrics = directory / "sequence_metrics.txt"
        if not metrics.is_file():
            continue
        frames = []
        for line in metrics.read_text(encoding="utf-8").splitlines():
            match = LINE.search(line)
            if match:
                frames.append({
                    "frame": int(match.group(1)),
                    "diff": float(match.group(3)),
                    "x": float(match.group(4)),
                    "y": float(match.group(5)),
                    "z": float(match.group(6)),
                })
        if len(frames) < 2:
            continue
        diffs = [f["diff"] for f in frames]
        mean = sum(diffs) / len(diffs)
        values = {
            "frames": len(frames),
            "mean_diff": mean,
            "max_diff": max(diffs),
            "std_diff": (sum((d - mean) ** 2 for d in diffs) / len(diffs)) ** 0.5,
            "start": [frames[0]["x"], frames[0]["y"], frames[0]["z"]],
            "end": [frames[-1]["x"], frames[-1]["y"], frames[-1]["z"]],
        }
        for label, distance in (("period", PERIOD), ("macro", MACRO)):
            x0, z0 = frames[0]["x"], frames[0]["z"]
            x1, z1 = frames[-1]["x"], frames[-1]["z"]
            if abs(x1 - x0) >= abs(z1 - z0):
                start, sign, coord = x0, 1.0 if x1 > x0 else -1.0, "x"
            else:
                start, sign, coord = z0, 1.0 if z1 > z0 else -1.0, "z"
            target = math.floor(start / distance) * distance
            if target <= start:
                target += distance
            frame = (target - start) / (sign * 25.0) if abs(sign) > 0.5 else None
            if frame is None or frame < 0 or frame >= len(frames):
                values[label] = None
                continue
            index = int(round(frame))
            window = diffs[max(0, index - 2):min(len(diffs), index + 3)]
            cross = max(window) if window else 0.0
            values[label] = {
                "axis": coord,
                "distance_m": distance,
                "frame": index,
                "cross_diff": cross,
                "ratio_to_mean": cross / mean if mean > 1e-9 else 0.0,
            }
        result[directory.name] = values
        print("%s: frames=%d mean=%.4f max=%.4f period=%s macro=%s" % (
            directory.name, len(frames), mean, values["max_diff"],
            values["period"], values["macro"]))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    print("out=%s sequences=%d" % (args.out, len(result)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
