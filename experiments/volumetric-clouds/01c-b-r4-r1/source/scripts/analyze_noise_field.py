#!/usr/bin/env python3
"""V2.3h-A01 raw noise-field analysis.

Reads the Float32 files written by --debug-mode=noise-field and reports:
  - raw channel statistics and the shape-transfer histogram;
  - horizontal/vertical autocorrelation peaks (in pixels and world metres);
  - vertical coherence at fixed lags;
  - texture-fetch count.

No third-party modules are required. The binary layout is documented in
noise_field_meta.txt and in Application::WriteNoiseFieldFiles.
"""

import argparse
import csv
import math
import struct
import sys
from pathlib import Path

HEADER_U32 = 8
HEADER_F32 = 8
HEADER_BYTES = 64
SIZE = 256


def load_field(path):
    data = path.read_bytes()
    if len(data) < HEADER_BYTES:
        raise ValueError("short file: %s" % path)
    (magic, version, rows, steps, records, axis, coord_mode,
     mix) = struct.unpack_from("<8I", data, 0)
    if magic != 0x4E465231:
        raise ValueError("bad magic in %s: 0x%08x" % (path, magic))
    if rows != SIZE or steps != SIZE or records != 3:
        raise ValueError("unexpected layout %dx%dx%d in %s"
                         % (rows, steps, records, path))
    (shape_low, shape_high, offset, extent_km, noise_scale,
     vertical_scale, layer_bottom, layer_top) = struct.unpack_from(
        "<8f", data, 32)
    count = rows * steps * records * 4
    values = struct.unpack_from("<%df" % count, data, HEADER_BYTES)
    return {
        "rows": rows,
        "steps": steps,
        "axis": axis,
        "coord_mode": coord_mode,
        "mix": mix,
        "shape_low": shape_low,
        "shape_high": shape_high,
        "offset": offset,
        "extent_km": extent_km,
        "noise_scale": noise_scale,
        "vertical_scale": vertical_scale,
        "layer_bottom": layer_bottom,
        "layer_top": layer_top,
        "values": values,
    }


def read_meta(path):
    meta = {}
    if not path.is_file():
        return meta
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if ":" in line:
            key, value = line.split(":", 1)
            meta[key.strip()] = value.strip()
    return meta


def channel_grid(field, index):
    rows = field["rows"]
    steps = field["steps"]
    values = field["values"]
    grid = []
    for y in range(rows):
        row = []
        base = y * steps * 12
        for x in range(steps):
            row.append(values[base + x * 12 + index])
        grid.append(row)
    return grid


def percentiles(values):
    ordered = sorted(values)
    n = len(ordered)

    def pct(p):
        i = int(round(max(0.0, min(1.0, p)) * (n - 1)))
        return ordered[i]

    return ordered[0], ordered[-1], pct(0.05), pct(0.50), pct(0.95)


def pearson(xs, ys):
    n = len(xs)
    if n < 2:
        return 0.0
    sx = sum(xs)
    sy = sum(ys)
    sxx = sum(v * v for v in xs)
    syy = sum(v * v for v in ys)
    sxy = sum(a * b for a, b in zip(xs, ys))
    numerator = n * sxy - sx * sy
    denominator = math.sqrt(max(n * sxx - sx * sx, 0.0) *
                            max(n * syy - sy * sy, 0.0))
    return numerator / denominator if denominator > 1e-12 else 0.0


def lag_curves(grid, max_lag, sample=4):
    rows = len(grid)
    cols = len(grid[0])
    horizontal = []
    vertical = []
    for lag in range(1, max_lag + 1):
        xs = []
        ys = []
        for y in range(0, rows, sample):
            for x in range(0, cols - lag, sample):
                xs.append(grid[y][x])
                ys.append(grid[y][x + lag])
        horizontal.append(pearson(xs, ys))

        xs = []
        ys = []
        for y in range(0, rows - lag, sample):
            for x in range(0, cols, sample):
                xs.append(grid[y][x])
                ys.append(grid[y + lag][x])
        vertical.append(pearson(xs, ys))
    return horizontal, vertical


def mask_metrics(grid, threshold=0.5):
    rows = len(grid)
    cols = len(grid[0])
    mask = [[1 if value >= threshold else 0 for value in row]
            for row in grid]
    occupied = sum(sum(row) for row in mask)
    if occupied == 0:
        return {
            "mask_frac": 0.0,
            "mask_h_run": 0.0,
            "mask_v_run": 0.0,
            "mask_v_bias": 0.0,
            "mask_v_long_frac": 0.0,
            "components": 0,
            "largest_frac": 0.0,
        }

    def runs():
        lengths = []
        for row in mask:
            run = 0
            for value in row:
                if value:
                    run += 1
                elif run:
                    lengths.append(run)
                    run = 0
            if run:
                lengths.append(run)
        return lengths

    horizontal = runs()
    vertical = []
    for x in range(cols):
        run = 0
        for y in range(rows):
            if mask[y][x]:
                run += 1
            elif run:
                vertical.append(run)
                run = 0
        if run:
            vertical.append(run)

    long_vertical = 0
    for x in range(cols):
        run = 0
        for y in range(rows + 1):
            if y < rows and mask[y][x]:
                run += 1
                continue
            if run >= 16:
                long_vertical += run
            run = 0

    seen = [[False] * cols for _ in range(rows)]
    components = 0
    largest = 0
    for y in range(rows):
        for x in range(cols):
            if not mask[y][x] or seen[y][x]:
                continue
            components += 1
            size = 0
            stack = [(y, x)]
            seen[y][x] = True
            while stack:
                cy, cx = stack.pop()
                size += 1
                for ny, nx in ((cy - 1, cx), (cy + 1, cx),
                               (cy, cx - 1), (cy, cx + 1)):
                    if (0 <= ny < rows and 0 <= nx < cols and
                            mask[ny][nx] and not seen[ny][nx]):
                        seen[ny][nx] = True
                        stack.append((ny, nx))
            largest = max(largest, size)

    h_mean = sum(horizontal) / len(horizontal) if horizontal else 0.0
    v_mean = sum(vertical) / len(vertical) if vertical else 0.0
    return {
        "mask_frac": occupied / float(rows * cols),
        "mask_h_run": h_mean,
        "mask_v_run": v_mean,
        "mask_v_bias": v_mean / h_mean if h_mean > 1e-9 else 0.0,
        "mask_v_long_frac": long_vertical / float(occupied),
        "components": components,
        "largest_frac": largest / float(occupied),
    }


def strongest_peak(curve, minimum_lag=4, threshold=0.05):
    best_lag = 0
    best_value = -2.0
    for index in range(minimum_lag, len(curve) - 1):
        value = curve[index]
        if value > curve[index - 1] and value >= curve[index + 1]:
            if value > best_value:
                best_value = value
                best_lag = index + 1
    if best_value < threshold:
        return 0, 0.0
    return best_lag, best_value


def half_life(curve):
    for index, value in enumerate(curve):
        if value < 0.5:
            return index + 1
    return len(curve)


def decorrelation_lag(curve, threshold=0.2):
    for index, value in enumerate(curve):
        if value < threshold:
            return index + 1
    return len(curve)


def corr_at_distance(curve, distance_m, pixel_m):
    if pixel_m <= 1e-9:
        return 0.0
    lag = distance_m / pixel_m
    low = int(math.floor(lag))
    high = low + 1
    if low < 1 or high > len(curve):
        return 0.0
    fraction = lag - low
    return curve[low - 1] * (1.0 - fraction) + curve[high - 1] * fraction


def block_repeat_ratio(grid, block_px):
    """RMS difference between adjacent world blocks, normalized by std.

    A value near 0 means adjacent blocks are exact copies (tiling). A value
    near 1.4 means independent blocks. This is a direct identity metric, not
    an autocorrelation peak.
    """
    rows = len(grid)
    cols = len(grid[0])
    block_px = max(2, int(block_px))
    if block_px * 2 > min(rows, cols):
        return 0.0
    mean = sum(v for row in grid for v in row) / (rows * cols)
    var = sum((v - mean) ** 2 for row in grid for v in row) / (rows * cols)
    std = math.sqrt(var)
    if std < 1e-9:
        return 0.0
    total = 0.0
    count = 0
    for by in range(0, rows - 2 * block_px, block_px):
        for bx in range(0, cols - 2 * block_px, block_px):
            for yy in range(block_px):
                for xx in range(block_px):
                    a = grid[by + yy][bx + xx]
                    b = grid[by + yy][bx + block_px + xx]
                    c = grid[by + block_px + yy][bx + xx]
                    total += (a - b) ** 2 + (a - c) ** 2
                    count += 2
    return math.sqrt(total / max(count, 1)) / std


def summarise(path, field, meta):
    shape = channel_grid(field, 3)
    density = channel_grid(field, 7)
    rows = field["rows"]
    cols = field["steps"]
    flat_shape = [v for row in shape for v in row]
    flat_density = [v for row in density for v in row]
    flat_r = [v for row in channel_grid(field, 0) for v in row]
    flat_b = [v for row in channel_grid(field, 2) for v in row]
    try:
        density_multiplier = float(meta.get("density_multiplier", "0.9"))
        extinction = float(meta.get("extinction", "0.021"))
        layer_bottom = float(meta.get("layer_bottom", "650"))
        layer_top = float(meta.get("layer_top", "2000"))
    except ValueError:
        density_multiplier = 0.9
        extinction = 0.021
        layer_bottom = 650.0
        layer_top = 2000.0
    thickness_m = max(layer_top - layer_bottom, 1.0)
    # The slice is not square in world units: XY/YZ map 2*extent km in the
    # horizontal direction and the layer thickness in the vertical direction
    # onto the same 256x256 image. Reporting pixel ratios without this
    # correction would misread the slice aspect ratio as cloud anisotropy.
    pixel_m_h = 2.0 * field["extent_km"] * 1000.0 / cols
    pixel_m_v = (thickness_m / rows
                 if field["axis"] in (0, 2) else pixel_m_h)
    r_min, r_max, r_p05, r_p50, r_p95 = percentiles(flat_r)
    b_min, b_max, b_p05, b_p50, b_p95 = percentiles(flat_b)
    s_min, s_max, s_p05, s_p50, s_p95 = percentiles(flat_shape)
    mean_shape = sum(flat_shape) / len(flat_shape)
    var_shape = sum((v - mean_shape) ** 2 for v in flat_shape) / len(flat_shape)
    transfer = 0.0
    for v in flat_shape:
        t = max(0.0, min(1.0, (v - field["shape_low"]) /
                          max(field["shape_high"] - field["shape_low"], 1e-6)))
        transfer += t * t * (3.0 - 2.0 * t)
    transfer /= len(flat_shape)

    max_lag = min(200, rows - 1)
    h_curve, v_curve = lag_curves(shape, max_lag, sample=4)
    h_lag, h_peak = strongest_peak(h_curve)
    v_lag, v_peak = strongest_peak(v_curve)
    r_h_curve, r_v_curve = lag_curves(
        channel_grid(field, 0), max_lag, sample=4)
    b_h_curve, b_v_curve = lag_curves(
        channel_grid(field, 2), max_lag, sample=4)
    a02_distances = (4530.0, 9060.0, 13600.0, 20000.0, 30000.0, 40000.0)
    a02_corr_h = {distance: corr_at_distance(h_curve, distance, pixel_m_h)
                  for distance in a02_distances}
    a02_corr_v = {distance: corr_at_distance(v_curve, distance, pixel_m_v)
                  for distance in a02_distances}
    block_4_53 = (block_repeat_ratio(shape, 4530.0 / pixel_m_h)
                  if field["axis"] == 1 else 0.0)
    block_9_06 = (block_repeat_ratio(shape, 9060.0 / pixel_m_h)
                  if field["axis"] == 1 else 0.0)

    column_od = []
    if field["axis"] in (0, 2):
        for x in range(cols):
            total = 0.0
            for y in range(rows):
                total += density[y][x]
            column_od.append(total * pixel_m_v * density_multiplier *
                             extinction)
        column_od.sort()
    mean_density = (sum(flat_density) / len(flat_density)
                    if flat_density else 0.0)
    density_nonzero = (sum(1 for value in flat_density if value > 1e-4) /
                       float(len(flat_density)))
    if column_od:
        od_mean = sum(column_od) / len(column_od)
        od_p50 = column_od[len(column_od) // 2]
        od_p95 = column_od[int(0.95 * (len(column_od) - 1))]
    else:
        od_mean = od_p50 = od_p95 = 0.0

    transfer_grid = []
    for row in shape:
        transfer_row = []
        for value in row:
            t = max(0.0, min(1.0, (value - field["shape_low"]) /
                              max(field["shape_high"] - field["shape_low"],
                                  1e-6)))
            transfer_row.append(t * t * (3.0 - 2.0 * t))
        transfer_grid.append(transfer_row)
    mask = mask_metrics(transfer_grid, 0.5)
    t_h_curve, t_v_curve = lag_curves(transfer_grid, max_lag, sample=4)
    d_h_curve, d_v_curve = lag_curves(density, max_lag, sample=4)
    transfer_corr_h = {distance: corr_at_distance(
        t_h_curve, distance, pixel_m_h) for distance in a02_distances}
    density_corr_h = {distance: corr_at_distance(
        d_h_curve, distance, pixel_m_h) for distance in a02_distances}
    transfer_block_4_53 = (block_repeat_ratio(
        transfer_grid, 4530.0 / pixel_m_h)
        if field["axis"] == 1 else 0.0)
    density_block_4_53 = (block_repeat_ratio(
        density, 4530.0 / pixel_m_h)
        if field["axis"] == 1 else 0.0)

    v_axis_name = "Y" if field["axis"] in (0, 2) else "Z"
    h_axis_name = "X" if field["axis"] in (0, 1) else "Z"
    coherence = [v_curve[min(lag, max_lag) - 1]
                 for lag in (16, 32, 64, 128)]

    def at(curve, lag):
        return curve[min(max(lag, 1), len(curve)) - 1]

    return {
        "directory": str(path.parent),
        "scene": meta.get("scene", ""),
        "seed": meta.get("noise_seed", ""),
        "mix": meta.get("noise_mix", str(field["mix"])),
        "field_mode": meta.get("field_mode", "direct"),
        "multi_scale": meta.get("multi_frequency_scale", ""),
        "multi_weight": meta.get("multi_frequency_weight", ""),
        "modulation_amplitude": meta.get("modulation_amplitude", ""),
        "coordinate": meta.get("coordinate_mode", str(field["coord_mode"])),
        "axis": ("xy", "xz", "yz")[field["axis"] % 3],
        "offset": field["offset"],
        "shape_low": field["shape_low"],
        "shape_high": field["shape_high"],
        "extent_km": field["extent_km"],
        "mean_r": sum(flat_r) / len(flat_r),
        "mean_b": sum(flat_b) / len(flat_b),
        "mean_shape": mean_shape,
        "std_shape": math.sqrt(var_shape),
        "p05_shape": s_p05,
        "p50_shape": s_p50,
        "p95_shape": s_p95,
        "mean_transfer": transfer,
        "r_min": r_min,
        "r_max": r_max,
        "r_p05": r_p05,
        "r_p50": r_p50,
        "r_p95": r_p95,
        "b_min": b_min,
        "b_max": b_max,
        "b_p05": b_p05,
        "b_p50": b_p50,
        "b_p95": b_p95,
        "h_axis": h_axis_name,
        "v_axis": v_axis_name,
        "h_peak_lag_px": h_lag,
        "h_peak_corr": h_peak,
        "v_peak_lag_px": v_lag,
        "v_peak_corr": v_peak,
        "h_period_m": h_lag * pixel_m_h,
        "v_period_m": v_lag * pixel_m_v,
        "h_corr_16": at(h_curve, 16),
        "h_corr_32": at(h_curve, 32),
        "h_corr_64": at(h_curve, 64),
        "v_corr_16": coherence[0],
        "v_corr_32": coherence[1],
        "v_corr_64": coherence[2],
        "v_corr_128": coherence[3],
        "shape_h_half_px": half_life(h_curve),
        "shape_v_half_px": half_life(v_curve),
        "shape_h_half_m": half_life(h_curve) * pixel_m_h,
        "shape_v_half_m": half_life(v_curve) * pixel_m_v,
        "shape_world_vh_half_ratio": (
            (half_life(v_curve) * pixel_m_v) /
            max(half_life(h_curve) * pixel_m_h, 1e-9)),
        "shape_h_decay_px": decorrelation_lag(h_curve),
        "shape_v_decay_px": decorrelation_lag(v_curve),
        "r_h_half_px": half_life(r_h_curve),
        "r_v_half_px": half_life(r_v_curve),
        "r_h_half_m": half_life(r_h_curve) * pixel_m_h,
        "r_v_half_m": half_life(r_v_curve) * pixel_m_v,
        "r_h_decay_px": decorrelation_lag(r_h_curve),
        "r_v_decay_px": decorrelation_lag(r_v_curve),
        "b_h_half_px": half_life(b_h_curve),
        "b_v_half_px": half_life(b_v_curve),
        "b_h_half_m": half_life(b_h_curve) * pixel_m_h,
        "b_v_half_m": half_life(b_v_curve) * pixel_m_v,
        "b_h_decay_px": decorrelation_lag(b_h_curve),
        "b_v_decay_px": decorrelation_lag(b_v_curve),
        "mask_frac": mask["mask_frac"],
        "mask_h_run_px": mask["mask_h_run"],
        "mask_v_run_px": mask["mask_v_run"],
        "mask_v_bias_px": mask["mask_v_bias"],
        "mask_h_run_m": mask["mask_h_run"] * pixel_m_h,
        "mask_v_run_m": mask["mask_v_run"] * pixel_m_v,
        "mask_world_v_bias": (
            (mask["mask_v_run"] * pixel_m_v) /
            max(mask["mask_h_run"] * pixel_m_h, 1e-9)),
        "mask_v_long_frac": mask["mask_v_long_frac"],
        "components": mask["components"],
        "largest_frac": mask["largest_frac"],
        "mean_density": mean_density,
        "density_nonzero_frac": density_nonzero,
        "column_od_mean": od_mean,
        "column_od_p50": od_p50,
        "column_od_p95": od_p95,
        "pixel_m_h": pixel_m_h,
        "pixel_m_v": pixel_m_v,
        "corr_4_53_h": a02_corr_h[4530.0],
        "corr_9_06_h": a02_corr_h[9060.0],
        "corr_13_6_h": a02_corr_h[13600.0],
        "corr_20_h": a02_corr_h[20000.0],
        "corr_30_h": a02_corr_h[30000.0],
        "corr_40_h": a02_corr_h[40000.0],
        "corr_4_53_v": a02_corr_v[4530.0],
        "corr_9_06_v": a02_corr_v[9060.0],
        "corr_13_6_v": a02_corr_v[13600.0],
        "corr_20_v": a02_corr_v[20000.0],
        "corr_30_v": a02_corr_v[30000.0],
        "corr_40_v": a02_corr_v[40000.0],
        "block_repeat_4_53": block_4_53,
        "block_repeat_9_06": block_9_06,
        "corr_4_53_t": transfer_corr_h[4530.0],
        "corr_9_06_t": transfer_corr_h[9060.0],
        "corr_13_6_t": transfer_corr_h[13600.0],
        "corr_20_t": transfer_corr_h[20000.0],
        "corr_30_t": transfer_corr_h[30000.0],
        "corr_40_t": transfer_corr_h[40000.0],
        "corr_4_53_d": density_corr_h[4530.0],
        "corr_9_06_d": density_corr_h[9060.0],
        "corr_13_6_d": density_corr_h[13600.0],
        "corr_20_d": density_corr_h[20000.0],
        "corr_30_d": density_corr_h[30000.0],
        "corr_40_d": density_corr_h[40000.0],
        "block_repeat_4_53_t": transfer_block_4_53,
        "block_repeat_4_53_d": density_block_4_53,
        "fetch_count": field["values"][11],
        "size": SIZE,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path,
                        default=Path("noise_field_analysis.csv"))
    args = parser.parse_args()

    fields = sorted(args.root.rglob("noise_field_float32.bin"))
    if not fields:
        print("no noise_field_float32.bin under %s" % args.root,
              file=sys.stderr)
        return 2

    rows = []
    for path in fields:
        try:
            field = load_field(path)
        except ValueError as error:
            print("skip %s: %s" % (path, error), file=sys.stderr)
            continue
        meta = read_meta(path.parent / "noise_field_meta.txt")
        rows.append(summarise(path, field, meta))

    if not rows:
        return 2

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        for row in rows:
            writer.writerow({key: (round(value, 8)
                                   if isinstance(value, float) else value)
                             for key, value in row.items()})

    print("fields=%d out=%s" % (len(rows), args.out))
    for row in rows:
        print("%-8s %-3s %-8s w=%-4s a=%-4s %-6s seed=%-5s "
              "tileT=%.3f/%.3f/%.3f blockT=%.2f"
              % (row["axis"], row["mix"], row["field_mode"],
                 row["multi_weight"], row["modulation_amplitude"],
                 row["coordinate"], row["seed"],
                 row["corr_4_53_t"], row["corr_9_06_t"],
                 row["corr_13_6_t"], row["block_repeat_4_53_t"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
