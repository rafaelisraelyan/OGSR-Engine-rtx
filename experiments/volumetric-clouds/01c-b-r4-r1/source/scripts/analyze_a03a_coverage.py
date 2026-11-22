#!/usr/bin/env python3
"""A03a C2/C3 coverage attribution.

Reads the A03a 4-channel density volume plus the 4-channel macro probe and
reports, per variant and weather preset:

  * column occupancy at rho > 0.2 (max over Y) and mean occupied thickness;
  * value distribution for macro_raw, macro_mass, detail_transfer,
    macro_mass_covered, shaped, envelope and density;
  * the coverage loss attributable to the detail multiplier and the
    post-detail coverage remap.

All numbers come from little-endian Float32 files, never from PNG.
"""

import argparse
import array
import json
import math
import sys
from pathlib import Path


def load_volume(directory, name, channels):
    meta_path = directory / (name + ".json")
    bin_path = directory / (name + ".bin")
    meta = json.load(open(meta_path, encoding="utf-8"))
    nx, ny, nz = meta["grid"]
    data = array.array("f")
    data.frombytes(bin_path.read_bytes())
    if len(data) != nx * ny * nz * channels:
        raise ValueError("%s: size mismatch %d != %d" %
                         (directory, len(data), nx * ny * nz * channels))
    return meta, data, nx, ny, nz


def percentile(values, q):
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round(q * (len(ordered) - 1)))))
    return ordered[index]


def column_stats(density, nx, ny, nz, threshold):
    occupied = 0
    exact_zero = 0
    below_epsilon = 0
    thicknesses = []
    maxes = []
    for z in range(nz):
        for x in range(nx):
            top = 0.0
            count = 0
            for y in range(ny):
                value = density[(z * ny + y) * nx + x]
                top = max(top, value)
                if value > threshold:
                    count += 1
            maxes.append(top)
            if top <= 0.0:
                exact_zero += 1
            if top <= 1e-4:
                below_epsilon += 1
            if count:
                occupied += 1
                thicknesses.append(count)
    return {
        "occupied_columns": occupied / float(nx * nz),
        "occupied_columns_count": occupied,
        "exact_zero_columns": exact_zero / float(nx * nz),
        "below_epsilon_columns": below_epsilon / float(nx * nz),
        "thickness_voxels_mean": (sum(thicknesses) / len(thicknesses)
                                  if thicknesses else 0.0),
        "thickness_voxels_std": ((sum((t - sum(thicknesses) /
                                       len(thicknesses)) ** 2
                                     for t in thicknesses) / len(thicknesses))
                                 ** 0.5 if thicknesses else 0.0),
        "column_max_mean": sum(maxes) / len(maxes) if maxes else 0.0,
        "column_max_p95": percentile(maxes, 0.95),
    }


def summarize(values, threshold=None):
    out = {
        "mean": sum(values) / len(values) if values else 0.0,
        "p50": percentile(values, 0.50),
        "p90": percentile(values, 0.90),
        "p99": percentile(values, 0.99),
        "max": max(values) if values else 0.0,
        "min": min(values) if values else 0.0,
    }
    if threshold is not None:
        out["fraction_gt"] = (sum(1 for v in values if v > threshold) /
                              len(values) if values else 0.0)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--threshold", type=float, default=0.2)
    args = parser.parse_args()

    result = {}
    for directory in sorted(p for p in args.root.iterdir() if p.is_dir()):
        density_path = directory / "density_volume_float32.json"
        probe_path = directory / "macro_probe_float32.json"
        if not density_path.is_file():
            continue
        dmeta, density, nx, ny, nz = load_volume(
            directory, "density_volume_float32", 4)
        shaped = density[1::4]
        envelope = density[2::4]
        dens = density[3::4]
        if probe_path.is_file():
            pmeta, probe, _, _, _ = load_volume(
                directory, "macro_probe_float32", 4)
            macro_raw = probe[0::4]
            mass = probe[1::4]
            detail = probe[2::4]
            covered = probe[3::4]
        else:
            pmeta = {
                "macro_coverage_preserve": 0,
                "macro_seed_hash": 0,
                "macro_seed": 0,
                "finite": True,
                "macro_detail_weight": 0.0,
            }
            macro_raw = mass = detail = covered = []

        threshold = args.threshold
        coverage_threshold = 1.0 - float(dmeta["coverage"])
        detail_weight = float(pmeta["macro_detail_weight"])
        # A03: occupancy is decided after mass*detail crosses the coverage
        # cut. Macro-only occupancy is decided by mass alone.
        macro_only_occ = sum(1 for v in mass if v > coverage_threshold)
        kernel = [m * (1.0 - detail_weight + detail_weight * d)
                  for m, d in zip(mass, detail)]
        killed_by_detail = sum(
            1 for m, k in zip(mass, kernel)
            if m > coverage_threshold and k <= coverage_threshold)
        result[directory.name] = {
            "grid": [nx, ny, nz],
            "coverage": dmeta["coverage"],
            "macro_coverage_preserve": pmeta["macro_coverage_preserve"],
            "macro_seed_hash": pmeta["macro_seed_hash"],
            "macro_seed": pmeta["macro_seed"],
            "finite": dmeta["finite"] and pmeta["finite"],
            "columns": column_stats(dens, nx, ny, nz, threshold),
            "columns_thresholds": {
                "0.05": column_stats(dens, nx, ny, nz, 0.05)["occupied_columns"],
                "0.10": column_stats(dens, nx, ny, nz, 0.10)["occupied_columns"],
                "0.20": column_stats(dens, nx, ny, nz, 0.20)["occupied_columns"],
                "0.50": column_stats(dens, nx, ny, nz, 0.50)["occupied_columns"],
            },
            "columns_shaped": column_stats(shaped, nx, ny, nz, threshold),
            "macro_raw": summarize(macro_raw, 0.42),
            "macro_mass": summarize(mass, coverage_threshold),
            "detail_transfer": summarize(detail),
            "macro_mass_covered": summarize(covered, coverage_threshold),
            "shaped": summarize(shaped, coverage_threshold),
            "envelope": summarize(envelope),
            "density": summarize(dens, threshold),
            "macro_only_voxel_fraction": (macro_only_occ / len(mass)
                                          if mass else 0.0),
            "detail_killed_voxel_fraction": (killed_by_detail / len(mass)
                                             if mass else 0.0),
            "density_voxel_fraction": (sum(1 for v in dens if v > threshold) /
                                       len(dens)),
        }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)

    print("%-12s %-8s %-8s %8s %8s %8s %8s" %
          ("variant", "cov", "hash", "cols", "shaped", "thick", "maxdens"))
    for name, row in result.items():
        print("%-12s %-8s %-8s %7.2f%% %7.2f%% %8.2f %8.3f" % (
            name, row["macro_coverage_preserve"], row["macro_seed_hash"],
            row["columns"]["occupied_columns"] * 100.0,
            row["columns_shaped"]["occupied_columns"] * 100.0,
            row["columns"]["thickness_voxels_mean"],
            row["density"]["max"]))
    print("out=%s variants=%d" % (args.out, len(result)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
