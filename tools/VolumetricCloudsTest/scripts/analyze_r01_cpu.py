#!/usr/bin/env python3
"""R01 CPU region-field analysis: components and lattice autocorrelation."""

import argparse
import array
import json
import math
import sys
from pathlib import Path

from r02_diagnostics import percentile


def load(directory, stem):
    meta = json.load(open(directory / (stem + ".json"), encoding="utf-8"))
    n = meta["grid"][0]
    data = array.array("f")
    data.frombytes((directory / (stem + ".bin")).read_bytes())
    return meta, data, n


def components(mask, n):
    seen = [False] * (n * n)
    areas = []
    for start in range(n * n):
        if not mask[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = True
        area = 0
        while stack:
            i = stack.pop()
            area += 1
            x = i % n
            z = i // n
            for dx, dz in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                xx, zz = x + dx, z + dz
                if xx < 0 or zz < 0 or xx >= n or zz >= n:
                    continue
                j = zz * n + xx
                if mask[j] and not seen[j]:
                    seen[j] = True
                    stack.append(j)
        areas.append(area)
    areas.sort()
    total = sum(areas)
    return {
        "dimension": 2,
        "connectivity": 4,
        "count": len(areas),
        "largest_fraction": areas[-1] / total if total else 0.0,
        "p50_area_cells": percentile(areas, 50.0),
        "p90_area_cells": percentile(areas, 90.0),
        "p95_area_cells": percentile(areas, 95.0),
        "percentile_method": "linear_interpolation_between_closest_ranks_0_based_ascending",
        "areas": areas,
    }


def corr(mask, n, lag):
    s = sx = sy = sxx = syy = 0
    cnt = 0
    for z in range(n):
        row = z * n
        for x in range(n - lag):
            v = mask[row + x]
            w = mask[row + x + lag]
            s += v * w
            sx += v
            sy += w
            sxx += v * v
            syy += w * w
            cnt += 1
    num = s - sx * sy / cnt
    den = math.sqrt((sxx - sx * sx / cnt) * (syy - sy * sy / cnt))
    return num / den if den > 0 else 0.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--stem", default="xzmax_s1337_c0.45")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    meta, data, n = load(args.directory, args.stem)
    mask = [1 if v > 0.2 else 0 for v in data]
    comp = components(mask, n)
    step = meta["cell_size_xz_m"]
    lags = {}
    for cells in (int(round(7000 / step)), int(round(14000 / step)),
                  int(round(4545.45 / step)), int(round(20000 / step))):
        if cells <= 0 or cells >= n:
            continue
        lags[str(cells)] = {
            "distance_m": cells * step,
            "corr": corr(mask, n, cells),
        }
    result = {
        "grid": [n, n],
        "cell_size_xz_m": step,
        "occupied_columns": sum(mask) / (n * n),
        "components": {k: v for k, v in comp.items() if k != "areas"},
        "autocorrelation": lags,
        "seed": meta["seed"],
        "cut": meta["cut"],
    }
    with open(args.out, "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
