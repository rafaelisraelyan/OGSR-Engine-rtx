#!/usr/bin/env python3
"""A02c independent 3D Storm density analysis.

Reads the Float32 dumps produced by --density-volume=1:
  density_volume_float32.bin/json   dense world-space grid
  density_columns_float32.bin/json  fine vertical columns (4 u32 samples per record)
  storm_boundary_float32.bin/json   analytic bottom/top at two sample heights

All thickness values are true world-space metres sampled along vertical
columns; none of them are screen projections or BMP brightness thresholds.
"""

import argparse
import array
import csv
import json
import math
import os
import statistics
import sys
from pathlib import Path


def load_bin(path, expect_records=None):
    data = array.array("f")
    data.frombytes(Path(path).read_bytes())
    if expect_records is not None and len(data) != expect_records * 4:
        raise ValueError("%s: expected %d records" % (path, expect_records))
    return data


def percentile(values, p):
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[int(max(0.0, min(1.0, p)) * (len(ordered) - 1))]


def stats(values):
    if not values:
        return {"count": 0}
    return {
        "count": len(values),
        "mean": statistics.mean(values),
        "std": statistics.pstdev(values) if len(values) > 1 else 0.0,
        "p50": percentile(values, 0.50),
        "p95": percentile(values, 0.95),
        "min": min(values),
        "max": max(values),
    }


def components_3d(mask, nx, ny, nz):
    seen = bytearray(len(mask))
    sizes = []
    stride_y = nx
    stride_z = nx * ny
    for start in range(len(mask)):
        if not mask[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        size = 0
        while stack:
            i = stack.pop()
            size += 1
            z, rem = divmod(i, stride_z)
            y, x = divmod(rem, stride_y)
            if x > 0 and mask[i - 1] and not seen[i - 1]:
                seen[i - 1] = 1
                stack.append(i - 1)
            if x + 1 < nx and mask[i + 1] and not seen[i + 1]:
                seen[i + 1] = 1
                stack.append(i + 1)
            if y > 0 and mask[i - stride_y] and not seen[i - stride_y]:
                seen[i - stride_y] = 1
                stack.append(i - stride_y)
            if y + 1 < ny and mask[i + stride_y] and not seen[i + stride_y]:
                seen[i + stride_y] = 1
                stack.append(i + stride_y)
            if z > 0 and mask[i - stride_z] and not seen[i - stride_z]:
                seen[i - stride_z] = 1
                stack.append(i - stride_z)
            if z + 1 < nz and mask[i + stride_z] and not seen[i + stride_z]:
                seen[i + stride_z] = 1
                stack.append(i + stride_z)
        sizes.append(size)
    sizes.sort(reverse=True)
    occupied = sum(sizes)
    return {
        "count": len(sizes),
        "occupied_voxels": occupied,
        "largest_fraction": sizes[0] / occupied if occupied else 0.0,
        "p95_component": percentile(sizes, 0.95),
        "sizes_top8": sizes[:8],
    }


def vertical_runs(occupied):
    runs = []
    gaps = []
    current = None
    start = 0
    i = 0
    while i < len(occupied):
        if occupied[i] and current is not True:
            if current is False and i > 0 and start > 0:
                gaps.append(i - start)
            current = True
            start = i
        elif not occupied[i] and current is True:
            runs.append(i - start)
            current = False
            start = i
        i += 1
    if current is True:
        runs.append(len(occupied) - start)
    return runs, gaps


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--columns-csv", type=Path, default=None)
    args = parser.parse_args()

    summary = {}
    column_rows = []
    for directory in sorted([p for p in args.root.iterdir() if p.is_dir()]):
        volume_meta_path = directory / "density_volume_float32.json"
        if not volume_meta_path.is_file():
            continue
        vmeta = json.load(open(volume_meta_path, encoding="utf-8"))
        nx, ny, nz = vmeta["grid"]
        cxz = vmeta["cell_size_xz_m"]
        cy = vmeta["cell_size_y_m"]
        volume = load_bin(directory / "density_volume_float32.bin", nx * ny * nz)

        bmeta_path = directory / "storm_boundary_float32.json"
        boundary_meta = json.load(open(bmeta_path, encoding="utf-8"))
        boundary = load_bin(directory / "storm_boundary_float32.bin")
        bottom_delta = []
        top_delta = []
        inverted = 0
        for i in range(len(boundary) // 4):
            b0, t0, b1, t1 = boundary[i * 4:i * 4 + 4]
            bottom_delta.append(abs(b0 - b1))
            top_delta.append(abs(t0 - t1))
            if t0 < b0 or t1 < b1:
                inverted += 1

        cmeta_path = directory / "density_columns_float32.json"
        cm = json.load(open(cmeta_path, encoding="utf-8"))
        columns = load_bin(directory / "density_columns_float32.bin")
        col_x, packets, col_z = cm["columns"][0], cm["packets"], cm["columns"][1]
        step = cm["column_y_step_m"]
        samples = packets * 4
        if len(columns) != col_x * packets * col_z * 4:
            raise ValueError("column size mismatch in %s" % directory)

        result = {
            "variant": directory.name,
            "scene": vmeta.get("variant"),
            "grid": [nx, ny, nz],
            "cell_size_xz_m": cxz,
            "cell_size_y_m": cy,
            "volume_bounds_m": [
                [vmeta["world_origin"][0], vmeta["world_origin"][0] + nx * cxz],
                [vmeta["world_origin"][1], vmeta["world_origin"][1] + ny * cy],
                [vmeta["world_origin"][2], vmeta["world_origin"][2] + nz * cxz],
            ],
            "storm_volume": vmeta.get("storm_volume"),
            "storm_column_envelope": vmeta.get("storm_column_envelope"),
            "boundary_audit": {
                "columns": len(bottom_delta),
                "bottom_delta_mean_m": statistics.mean(bottom_delta),
                "bottom_delta_max_m": max(bottom_delta),
                "top_delta_mean_m": statistics.mean(top_delta),
                "top_delta_max_m": max(top_delta),
                "inverted_pairs": inverted,
            },
            "thresholds": {},
            "columns": {},
        }

        for threshold in (0.05, 0.1, 0.2, 0.5):
            mask = bytearray(1 if volume[i * 4 + 3] >= threshold else 0
                             for i in range(nx * ny * nz))
            occupied = sum(mask)
            # Coarse per-column first/last span and vertical run count.
            spans = []
            multi = 0
            runs_total = 0
            for z in range(nz):
                for x in range(nx):
                    ys = [y for y in range(ny)
                          if mask[(z * ny + y) * nx + x]]
                    if not ys:
                        spans.append(0.0)
                        continue
                    spans.append((max(ys) - min(ys) + 1) * cy)
                    run_count = 1
                    for i in range(1, len(ys)):
                        if ys[i] != ys[i - 1] + 1:
                            run_count += 1
                    runs_total += run_count
                    if run_count > 1:
                        multi += 1
            occupied_spans = [s for s in spans if s > 0]
            by_height = []
            for y in range(ny):
                plane = [volume[((z * ny + y) * nx + x) * 4 + 3]
                         for z in range(nz) for x in range(nx)]
                by_height.append({
                    "y_m": vmeta["world_origin"][1] + (y + 0.5) * cy,
                    "mean_density": statistics.mean(plane),
                    "p95_density": percentile(plane, 0.95),
                    "occupied_fraction": sum(1 for v in plane if v >= threshold) / len(plane),
                })
            comp = components_3d(mask, nx, ny, nz)
            result["thresholds"][str(threshold)] = {
                "occupied_voxel_fraction": occupied / (nx * ny * nz),
                "occupied_columns": len(occupied_spans) / len(spans),
                "span_stats_m": stats(occupied_spans),
                "multi_run_column_fraction": multi / len(spans),
                "mean_runs_per_column": runs_total / len(spans),
                "components_3d": comp,
                "density_by_height": by_height,
            }

            # Fine columns: true metric vertical sampling at ~4.2 m.
            thickness = []
            run_counts = []
            gaps = []
            for z in range(col_z):
                for x in range(col_x):
                    occupied_samples = []
                    for p in range(packets):
                        base = ((z * packets + p) * col_x + x) * 4
                        for k in range(4):
                            if columns[base + k] >= threshold:
                                occupied_samples.append(p * 4 + k)
                    occ = [False] * samples
                    for s in occupied_samples:
                        occ[s] = True
                    runs, empty = vertical_runs(occ)
                    gaps.extend(empty)
                    run_counts.append(len(runs))
                    if occupied_samples:
                        thickness_m = (occupied_samples[-1] - occupied_samples[0] + 1) * step
                    else:
                        thickness_m = 0.0
                    thickness.append(thickness_m)
                    column_rows.append({
                        "variant": result["variant"],
                        "threshold": threshold,
                        "x_index": x,
                        "z_index": z,
                        "thickness_m": round(thickness_m, 3),
                        "samples": len(occupied_samples),
                    })
            occupied_thickness = [t for t in thickness if t > 0]
            result["columns"][str(threshold)] = {
                "samples_per_column": samples,
                "sample_step_m": step,
                "occupied_columns": len(occupied_thickness) / len(thickness),
                "thickness_all_m": stats(thickness),
                "thickness_occupied_m": stats(occupied_thickness),
                "mean_runs_per_column": statistics.mean(run_counts),
                "multi_run_fraction": sum(1 for r in run_counts if r > 1) / len(run_counts),
                "gap_lengths_m": stats([g * step for g in gaps]),
            }

        # Vertical correlation length from the fine columns (density). A 1-in-8
        # column sample keeps the pure-Python pass bounded while preserving a
        # representative average.
        lags = list(range(1, 129))
        sums = {lag: 0.0 for lag in lags}
        counts = {lag: 0 for lag in lags}
        for z in range(0, col_z, 8):
            for x in range(0, col_x, 8):
                values = [0.0] * samples
                for p in range(packets):
                    base = ((z * packets + p) * col_x + x) * 4
                    for k in range(4):
                        values[p * 4 + k] = columns[base + k]
                mean = statistics.mean(values)
                var = sum((v - mean) ** 2 for v in values)
                if var <= 1e-12:
                    continue
                for lag in lags:
                    total = 0.0
                    for i in range(samples - lag):
                        total += (values[i] - mean) * (values[i + lag] - mean)
                    sums[lag] += total / var
                    counts[lag] += 1
        vertical = []
        for lag in lags:
            if counts[lag]:
                vertical.append({"lag": lag, "distance_m": lag * step,
                                 "corr": sums[lag] / counts[lag]})
        result["vertical_correlation"] = vertical
        decay = next((v["distance_m"] for v in vertical if v["corr"] < 1.0 / math.e),
                     None)
        result["vertical_correlation_length_m"] = (
            decay if decay is not None else vertical[-1]["distance_m"])
        result["vertical_correlation_censored"] = decay is None

        # Horizontal correlation of the top-down maximum density. A single
        # Y plane is often near-empty, so the max projection is the stable
        # structural field for a horizontal correlation length.
        plane = []
        for z in range(nz):
            for x in range(nx):
                best = 0.0
                for y in range(ny):
                    value = volume[((z * ny + y) * nx + x) * 4 + 3]
                    if value > best:
                        best = value
                plane.append(best)
        plane_mean = statistics.mean(plane)
        plane_var = sum((v - plane_mean) ** 2 for v in plane)
        horizontal = []
        for lag in range(1, 33):
            total = 0.0
            count = 0
            for z in range(nz):
                for x in range(nx - lag):
                    a = plane[z * nx + x] - plane_mean
                    b = plane[z * nx + x + lag] - plane_mean
                    total += a * b
                    count += 1
            corr = total / plane_var if plane_var > 1e-12 else 0.0
            horizontal.append({"lag": lag, "distance_m": lag * cxz,
                               "corr": corr})
        result["horizontal_correlation"] = horizontal
        hdecay = next((v["distance_m"] for v in horizontal
                       if v["corr"] < 1.0 / math.e), None)
        result["horizontal_correlation_length_m"] = (
            hdecay if hdecay is not None else horizontal[-1]["distance_m"])
        result["horizontal_correlation_censored"] = hdecay is None
        summary[result["variant"]] = result
        print("analyzed", result["variant"])

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
    if args.columns_csv is not None:
        with args.columns_csv.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(column_rows[0].keys()))
            writer.writeheader()
            writer.writerows(column_rows)
    print("out=%s variants=%d" % (args.out, len(summary)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
