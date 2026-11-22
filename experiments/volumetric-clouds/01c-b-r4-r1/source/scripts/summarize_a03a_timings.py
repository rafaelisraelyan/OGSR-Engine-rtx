#!/usr/bin/env python3
"""Correct GPU timing summary for V2.3h-A03a.

Each run reports the renderer's per-frame median and p95. This tool never
substitutes a maximum for a median: it keeps the per-run values, then reports
the median/min/max of the run medians and the median/max of the run p95s.
"""

import argparse
import csv
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def type7(values, fraction):
    ordered = sorted(values)
    if not ordered:
        return 0.0
    position = max(0.0, min(1.0, fraction)) * (len(ordered) - 1)
    lower = int(position)
    upper = min(len(ordered) - 1, lower + 1)
    weight = position - lower
    return ordered[lower] + (ordered[upper] - ordered[lower]) * weight


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("csv_path", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--cloudiness", type=Path,
                        help="JSON with per-model occupied_fraction and mean_od")
    args = parser.parse_args()

    rows = list(csv.DictReader(open(args.csv_path, encoding="utf-8-sig")))
    groups = defaultdict(list)
    for row in rows:
        key = (row["Model"], row["Preset"], int(row["Steps"]),
               int(row["Shadow"]))
        groups[key].append(row)

    cloudiness = {}
    if args.cloudiness and args.cloudiness.is_file():
        cloudiness = json.load(open(args.cloudiness, encoding="utf-8"))

    summary = []
    for key in sorted(groups):
        model, preset, steps, shadow = key
        runs = groups[key]
        medians = [float(r["CloudMedianMs"].replace(",", ".")) for r in runs]
        p95s = [float(r["CloudP95Ms"].replace(",", ".")) for r in runs]
        entry = {
            "model": model,
            "preset": preset,
            "steps": steps,
            "shadow_steps": shadow,
            "runs": len(runs),
            "run_medians_ms": medians,
            "run_p95_ms": p95s,
            "median_of_run_medians_ms": type7(medians, 0.5),
            "min_of_run_medians_ms": min(medians),
            "max_of_run_medians_ms": max(medians),
            "median_of_run_p95_ms": type7(p95s, 0.5),
            "max_of_run_p95_ms": max(p95s),
            "run_min_ms": [float(r["CloudMinMs"].replace(",", ".")) for r in runs],
            "run_max_ms": [float(r["CloudMaxMs"].replace(",", ".")) for r in runs],
        }
        if model in cloudiness:
            entry.update(cloudiness[model])
        summary.append(entry)

    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)

    print("%-18s %-9s %5s %4s %9s %9s %9s %9s" %
          ("model", "preset", "steps", "n", "median", "min", "max", "p95med"))
    for e in summary:
        print("%-18s %-9s %5d %4d %9.3f %9.3f %9.3f %9.3f" % (
            e["model"], e["preset"], e["steps"], e["runs"],
            e["median_of_run_medians_ms"], e["min_of_run_medians_ms"],
            e["max_of_run_medians_ms"], e["median_of_run_p95_ms"]))
    print("out=%s groups=%d" % (args.out, len(summary)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
