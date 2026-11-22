#!/usr/bin/env python3
"""World-space autocorrelation of A03a 60 km slices.

Reads a 512 x 1 x 512 Float32 probe and reports normalized autocorrelation
along X and Z at the old NoiseVolume period (4545.45 m) and its multiples,
plus the strongest non-zero lag. A periodic field peaks at the exact period;
a non-tiling macro field does not.
"""

import argparse
import array
import json
import math
import sys
from pathlib import Path


def load(path, channel, channels):
    meta = json.load(open(path.with_name(path.name.replace(".bin", ".json")),
                          encoding="utf-8"))
    nx, ny, nz = meta["grid"]
    data = array.array("f")
    data.frombytes(path.read_bytes())
    values = data[channel::channels]
    if len(values) != nx * nz:
        raise ValueError("size mismatch: %s" % path)
    return meta, values, nx, nz


def corr_at(values, nx, nz, dx, dz):
    total = 0.0
    count = 0
    x_end = nx - abs(dx)
    z_end = nz - abs(dz)
    for z in range(z_end):
        base = z * nx
        other = (z + dz) * nx + dx
        for x in range(x_end):
            total += values[base + x] * values[other + x]
            count += 1
    return total / count if count else 0.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("bin_path", type=Path)
    parser.add_argument("--channel", type=int, default=0)
    parser.add_argument("--channels", type=int, default=4)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    meta, values, nx, nz = load(args.bin_path, args.channel, args.channels)
    cell = float(meta["cell_size_xz_m"])
    mean = sum(values) / len(values)
    var = sum((v - mean) ** 2 for v in values) / len(values)
    if var <= 0.0:
        raise ValueError("flat field")

    def norm(dx, dz):
        return (corr_at(values, nx, nz, dx, dz) - mean * mean) / var

    period_cells = 4545.454545454545 / cell
    lags = [int(round(period_cells * k)) for k in (1, 2, 3)]
    result = {
        "source": str(args.bin_path),
        "cell_size_xz_m": cell,
        "grid": [nx, nz],
        "world_extent_m": [nx * cell, nz * cell],
        "period_cells_4545": period_cells,
        "lag_correlation_x": {str(lag): norm(lag, 0) for lag in range(1, 81)},
        "lag_correlation_z": {str(lag): norm(0, lag) for lag in range(1, 81)},
        "period_x": {str(lag): norm(lag, 0) for lag in lags},
        "period_z": {str(lag): norm(0, lag) for lag in lags},
    }
    best_x = max(range(2, 81), key=lambda lag: norm(lag, 0))
    best_z = max(range(2, 81), key=lambda lag: norm(0, lag))
    result["strongest_lag_x_cells"] = best_x
    result["strongest_lag_z_cells"] = best_z
    result["strongest_lag_x_m"] = best_x * cell
    result["strongest_lag_z_m"] = best_z * cell
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    print("%s: period %.2f cells | x corr %s | strongest x %d (%.0f m) z %d (%.0f m)" % (
        args.bin_path.parent.name, period_cells,
        ", ".join("%.3f" % result["period_x"][str(lag)] for lag in lags),
        best_x, best_x * cell, best_z, best_z * cell))
    return 0


if __name__ == "__main__":
    sys.exit(main())
