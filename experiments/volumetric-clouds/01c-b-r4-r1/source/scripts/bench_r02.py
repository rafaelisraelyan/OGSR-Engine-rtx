#!/usr/bin/env python3
"""R02 CPU complexity measurement (no GPU claim)."""

import json
import random
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import proto_r02_sites as sites
import proto_r02_systems as systems


def bench(fn, count):
    started = time.perf_counter()
    for _ in range(count):
        fn()
    return time.perf_counter() - started


def main():
    count = 200000
    rng = random.Random(1337)
    columns = []
    for _ in range(count):
        x = rng.uniform(-8000.0, 8000.0)
        z = rng.uniform(-8000.0, 8000.0)
        columns.append((x, z, rng.uniform(400.0, 5800.0)))

    h1_cols = [systems.column_context(x, z, 1337, 1.0) for x, z, _ in columns]
    h2_cols = [sites.column_context(x, z, 1337) for x, z, _ in columns]

    h1_col_time = bench(lambda: systems.column_context(1.0, 2.0, 1337, 1.0), count)
    h1_vox_time = None
    for i in range(count):
        systems.sample_voxel(columns[i][2], h1_cols[i], 1337)
    started = time.perf_counter()
    for i in range(count):
        systems.sample_voxel(columns[i][2], h1_cols[i], 1337)
    h1_vox_time = time.perf_counter() - started

    h2_col_time = bench(lambda: sites.column_context(1.0, 2.0, 1337), count)
    started = time.perf_counter()
    for i in range(count):
        sites.sample_site(columns[i][2], h2_cols[i], 1337)
    h2_site_time = time.perf_counter() - started

    # Full per-sample path with fresh column context, as a future HLSL sample
    # would have no CPU-side cache.
    def h1_full(i):
        col = systems.column_context(columns[i][0], columns[i][1], 1337, 1.0)
        systems.sample_voxel(columns[i][2], col, 1337)

    def h2_full(i):
        col = sites.column_context(columns[i][0], columns[i][1], 1337)
        sites.sample_site(columns[i][2], col, 1337)

    started = time.perf_counter()
    for i in range(count):
        h1_full(i)
    h1_full_time = time.perf_counter() - started

    started = time.perf_counter()
    for i in range(count):
        h2_full(i)
    h2_full_time = time.perf_counter() - started

    result = {
        "samples": count,
        "cpu_only": True,
        "gpu_cost_gate": "NOT RUN",
        "hcs1": {
            "ms_per_1m_full_samples": h1_full_time * 1e6 / count * 1000.0,
            "ms_per_1m_column_context": h1_col_time * 1e6 / count * 1000.0,
            "ms_per_1m_voxel_only": h1_vox_time * 1e6 / count * 1000.0,
            "samples_per_second_full": count / h1_full_time,
            "noise2_calls_per_sample": 17,
            "noise3_calls_per_sample": 3,
            "hash2_evaluations_per_sample": 68,
            "hash3_evaluations_per_sample": 24,
            "total_hash_evaluations_per_sample": 92,
            "max_sites_per_sample": 0,
            "max_site_distance_evaluations": 0,
            "note": "column context: 4 warp + 3 system + 3 mass + 5 height; voxel: 2 shear + 3 octaves of 3D mass",
        },
        "hcs2": {
            "ms_per_1m_full_samples": h2_full_time * 1e6 / count * 1000.0,
            "ms_per_1m_column_context": h2_col_time * 1e6 / count * 1000.0,
            "ms_per_1m_site_only": h2_site_time * 1e6 / count * 1000.0,
            "samples_per_second_full": count / h2_full_time,
            "noise2_calls_per_sample": 7,
            "noise3_calls_per_sample": 0,
            "hash2_evaluations_per_sample": 28,
            "hash3_evaluations_per_sample": 0,
            "max_sites_per_sample": 18,
            "max_site_distance_evaluations": 18,
            "note": "4 warp + 2 system + 1 ridge; 18 sites from 3x3 neighbourhoods of two grids",
        },
    }
    out = Path(__file__).resolve().parents[1] / "artifacts" / "v23h_a03b_r02" / "final" / "complexity"
    out.mkdir(parents=True, exist_ok=True)
    with open(out / "complexity_r02.json", "w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
        handle.write("\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
