#!/usr/bin/env python3
"""Choose comparable H2 thresholds and H3 coverage for r/b/rb.

The raw noise-field captures are made at a fixed diagnostic transfer
(0.58..0.88) and H3 coverage 0.45. This script reads the raw shapeField and
profile from the YZ/offset 0.5/seed 1337 captures and solves for controls
that match the current 0.70R+0.30B baseline:

  H2: fixed transfer width 0.30, solve the lower edge so mean(smoothstep)
      matches the baseline mean transfer.
  H3: solve coverage so mean(saturate((shape-(1-c))/c) * profile) matches
      the baseline at coverage 0.45.

It also reports the occupied mask fraction and a vertical-run bias at the
matched H2 threshold.
"""

import argparse
import csv
import math
import struct
import sys
from pathlib import Path

SIZE = 256


def load_field(path):
    data = path.read_bytes()
    (magic, version, rows, steps, records, axis, coord_mode,
     mix) = struct.unpack_from("<8I", data, 0)
    if magic != 0x4E465231 or rows != SIZE or steps != SIZE:
        raise ValueError("bad field: %s" % path)
    header = struct.unpack_from("<8f", data, 32)
    count = rows * steps * records * 4
    values = struct.unpack_from("<%df" % count, data, 64)
    return {
        "values": values,
        "shape_low": header[0],
        "shape_high": header[1],
        "extent_km": header[3],
        "layer_bottom": header[6],
        "layer_top": header[7],
        "rows": rows,
        "steps": steps,
    }


def shape_profile(values):
    shape = []
    profile = []
    for index in range(SIZE * SIZE):
        base = index * 12
        shape.append(values[base + 3])
        profile.append(values[base + 5])
    return shape, profile


def smoothstep(value, low, high):
    t = max(0.0, min(1.0, (value - low) / max(high - low, 1e-6)))
    return t * t * (3.0 - 2.0 * t)


def mean_transfer(shape, low, high):
    return sum(smoothstep(value, low, high) for value in shape) / len(shape)


def stage3_density(shape, profile, coverage):
    if coverage <= 1e-6:
        return 0.0
    total = 0.0
    for value, p in zip(shape, profile):
        remap = max(0.0, min(1.0, (value - (1.0 - coverage)) / coverage))
        total += remap * p
    return total / len(shape)


def solve_down(function, target, low, high, iterations=48):
    """Solve a monotonically decreasing function."""
    for _ in range(iterations):
        middle = (low + high) * 0.5
        if function(middle) > target:
            low = middle
        else:
            high = middle
    return (low + high) * 0.5


def solve_up(function, target, low, high, iterations=48):
    """Solve a monotonically increasing function (H3 coverage)."""
    for _ in range(iterations):
        middle = (low + high) * 0.5
        if function(middle) < target:
            low = middle
        else:
            high = middle
    return (low + high) * 0.5


def mask_stats(shape, low, high):
    mask = [1 if smoothstep(value, low, high) >= 0.5 else 0
            for value in shape]
    occupied = sum(mask)
    if occupied == 0:
        return 0.0, 0.0, 0.0, 0.0

    horizontal = []
    for y in range(SIZE):
        run = 0
        for x in range(SIZE):
            if mask[y * SIZE + x]:
                run += 1
            elif run:
                horizontal.append(run)
                run = 0
        if run:
            horizontal.append(run)

    vertical = []
    long_vertical = 0
    for x in range(SIZE):
        run = 0
        for y in range(SIZE):
            if mask[y * SIZE + x]:
                run += 1
            elif run:
                vertical.append(run)
                if run >= 16:
                    long_vertical += run
                run = 0
        if run:
            vertical.append(run)
            if run >= 16:
                long_vertical += run

    h_mean = sum(horizontal) / len(horizontal) if horizontal else 0.0
    v_mean = sum(vertical) / len(vertical) if vertical else 0.0
    return (occupied / float(SIZE * SIZE), h_mean, v_mean,
            long_vertical / float(occupied))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path,
                        help="artifacts/v23h_a01/noise_fields/plain")
    parser.add_argument("--out", type=Path,
                        default=Path("matched_controls.csv"))
    args = parser.parse_args()

    fields = {}
    for mix in ("r", "b", "rb"):
        path = (args.root / mix / "yz" / "off0.50" / "seed1337" /
                "noise_field_float32.bin")
        if not path.is_file():
            raise SystemExit("missing %s" % path)
        field = load_field(path)
        shape, profile = shape_profile(field["values"])
        fields[mix] = (field, shape, profile)

    baseline_field, baseline_shape, baseline_profile = fields["rb"]
    baseline_transfer = mean_transfer(
        baseline_shape, baseline_field["shape_low"],
        baseline_field["shape_high"])
    baseline_density = stage3_density(
        baseline_shape, baseline_profile, 0.45)

    rows = []
    for mix in ("r", "b", "rb"):
        field, shape, profile = fields[mix]
        low = solve_down(lambda value: mean_transfer(shape, value, value + 0.30),
                         baseline_transfer, 0.0, 0.95)
        high = low + 0.30
        transfer = mean_transfer(shape, low, high)
        mask_frac, h_run, v_run, long_v = mask_stats(shape, low, high)
        pixel_m_h = 2.0 * field["extent_km"] * 1000.0 / SIZE
        pixel_m_v = ((field["layer_top"] - field["layer_bottom"]) / SIZE)
        world_bias = ((v_run * pixel_m_v) /
                      max(h_run * pixel_m_h, 1e-9))
        coverage = solve_up(
            lambda value: stage3_density(shape, profile, value),
            baseline_density, 0.0, 1.0)
        density = stage3_density(shape, profile, coverage)
        rows.append({
            "mix": mix,
            "h2_shape_low": round(low, 4),
            "h2_shape_high": round(high, 4),
            "h2_mean_transfer": round(transfer, 6),
            "h2_mask_frac": round(mask_frac, 6),
            "h2_mask_h_run_px": round(h_run, 3),
            "h2_mask_v_run_px": round(v_run, 3),
            "h2_mask_v_bias_px": round(
                v_run / h_run if h_run > 1e-9 else 0.0, 3),
            "h2_mask_h_run_m": round(h_run * pixel_m_h, 3),
            "h2_mask_v_run_m": round(v_run * pixel_m_v, 3),
            "h2_mask_world_v_bias": round(world_bias, 3),
            "h2_mask_v_long_frac": round(long_v, 6),
            "h3_coverage": round(coverage, 4),
            "h3_mean_density": round(density, 6),
        })

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    print("baseline transfer=%.6f density=%.6f" %
          (baseline_transfer, baseline_density))
    print("mix  H2 low/high     transfer  mask    h_run_m v_run_m  "
          "V/H(world)  long_v  H3 coverage  density")
    for row in rows:
        print("%-3s  %.4f/%.4f  %.6f  %.4f  %6.1f  %6.1f  %5.2f       "
              "%.3f   %.4f       %.6f"
              % (row["mix"], row["h2_shape_low"], row["h2_shape_high"],
                 row["h2_mean_transfer"], row["h2_mask_frac"],
                 row["h2_mask_h_run_m"], row["h2_mask_v_run_m"],
                 row["h2_mask_world_v_bias"], row["h2_mask_v_long_frac"],
                 row["h3_coverage"], row["h3_mean_density"]))
    print("out=%s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
