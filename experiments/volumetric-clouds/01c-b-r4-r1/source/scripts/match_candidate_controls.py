#!/usr/bin/env python3
"""Solve matched H2 transfer edges and H3 coverage for an A02 candidate.

The target mean transfer and mean H3 density come from the direct R-only
baseline at the same world slice / seed. Keeping these values comparable is
required for a fair uniqueness comparison.
"""

import argparse
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import match_reference_mixes as m


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("field", type=Path)
    parser.add_argument("--target-transfer", type=float, required=True)
    parser.add_argument("--target-density", type=float, required=True)
    parser.add_argument("--width", type=float, default=0.30)
    parser.add_argument("--out", type=Path, default=Path("candidate_controls.csv"))
    args = parser.parse_args()

    field = m.load_field(args.field)
    shape, profile = m.shape_profile(field["values"])
    low = m.solve_down(
        lambda value: m.mean_transfer(shape, value, value + args.width),
        args.target_transfer, 0.0, 0.98)
    high = low + args.width
    transfer = m.mean_transfer(shape, low, high)
    mask_frac, h_run, v_run, long_v = m.mask_stats(shape, low, high)
    pixel_m_h = 2.0 * field["extent_km"] * 1000.0 / m.SIZE
    pixel_m_v = (field["layer_top"] - field["layer_bottom"]) / m.SIZE
    world_bias = (v_run * pixel_m_v) / max(h_run * pixel_m_h, 1e-9)
    coverage = m.solve_up(
        lambda value: m.stage3_density(shape, profile, value),
        args.target_density, 0.0, 1.0)
    density = m.stage3_density(shape, profile, coverage)

    row = {
        "field": str(args.field),
        "target_transfer": args.target_transfer,
        "target_density": args.target_density,
        "shape_low": round(low, 4),
        "shape_high": round(high, 4),
        "mean_transfer": round(transfer, 6),
        "mask_frac": round(mask_frac, 6),
        "mask_h_run_m": round(h_run * pixel_m_h, 3),
        "mask_v_run_m": round(v_run * pixel_m_v, 3),
        "mask_world_v_bias": round(world_bias, 3),
        "mask_v_long_frac": round(long_v, 6),
        "coverage": round(coverage, 4),
        "mean_density": round(density, 6),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(row.keys()))
        writer.writeheader()
        writer.writerow(row)
    for key, value in row.items():
        print("%s=%s" % (key, value))
    return 0


if __name__ == "__main__":
    sys.exit(main())
