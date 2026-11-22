#!/usr/bin/env python3
"""R02 hierarchical cloud-system CPU evaluator.

Research prototype only. It is deliberately independent of the game runtime
and of the R01 region evaluator. No HLSL, no GPU, no WeatherCycles.

Hierarchy
---------
1. Cloud system: large non-periodic domain-warped field (blobs + curved
   ridges) at tens of kilometres.
2. Mass organization: mid-scale warped billow/FBM field inside the system.
3. Vertical development: per-column base/top envelope, variable top softness
   and a 3D mass field whose horizontal pattern changes with altitude.
4. Fine erosion: intentionally absent.

The spatial hashing lattice is only the value-noise domain. It has no wrap and
its cell structure is smoothed by quintic interpolation; it is not a region
placement lattice and does not appear in the output by construction. The
prototype measures whether that claim survives visual and statistical review.
"""

import argparse
import array
import json
import math
import sys
import time
from pathlib import Path

from r02_diagnostics import (
    color_map,
    component_stats_2d,
    component_stats_3d,
    percentile,
    png_write,
    resample_nearest,
    rgb_rows_to_buffer,
    sha256_file,
    validate_field_metadata,
    world_aspect_size,
    write_float32_field,
)

MASK32 = 0xFFFFFFFF
VERSION = "OGSR_V2_3h_A03b_R02_HCS1"

# Fixed algorithm parameters. Weather changes only coverage, height scale and
# density gain; it never changes the spatial organisation algorithm.
SYSTEM_SCALE_M = 52000.0
SYSTEM_RIDGE_SCALE_M = 30000.0
MASS_SCALE_M = 3200.0
MASS_RIDGE_SCALE_M = 6000.0
MASS3_HORIZONTAL_SCALE_M = 2500.0
VERTICAL_SCALE_M = 700.0
SHEAR_AMPLITUDE_M = 650.0
SHEAR_VERTICAL_SCALE_M = 1500.0
SHEAR_HORIZONTAL_SCALE_M = 3200.0
WARP_COARSE_M = 6200.0
WARP_COARSE_SCALE_M = 38000.0
WARP_FINE_M = 1500.0
WARP_FINE_SCALE_M = 12500.0
BOTTOM_BASE_M = 1300.0
BOTTOM_SYSTEM_M = 450.0
BOTTOM_FINE_M = 300.0
THICK_BASE_M = 2400.0
THICK_SYSTEM_M = 800.0
THICK_FINE_M = 600.0
THICK_COVERAGE_M = 400.0

OCCUPANCY_THRESHOLD = 0.2


def clamp01(v):
    if v < 0.0:
        return 0.0
    if v > 1.0:
        return 1.0
    return v


def smoothstep(a, b, x):
    if b <= a:
        return 0.0 if x < a else 1.0
    t = (x - a) / (b - a)
    t = clamp01(t)
    return t * t * (3.0 - 2.0 * t)


def _hash2(ix, iz, seed, salt):
    h = (ix * 374761393 + iz * 668265263 + seed * 2246822519 + salt * 3266489917) & MASK32
    h ^= h >> 15
    h = (h * 0x2C1B3C6D) & MASK32
    h ^= h >> 12
    h = (h * 0x297A2D39) & MASK32
    h ^= h >> 15
    return h / 4294967296.0


def _hash3(ix, iy, iz, seed, salt):
    h = (ix * 374761393 + iy * 1440662683 + iz * 668265263 +
         seed * 2246822519 + salt * 3266489917) & MASK32
    h ^= h >> 15
    h = (h * 0x2C1B3C6D) & MASK32
    h ^= h >> 12
    h = (h * 0x297A2D39) & MASK32
    h ^= h >> 15
    return h / 4294967296.0


def noise2(x, z, seed, salt):
    ix = math.floor(x)
    iz = math.floor(z)
    fx = x - ix
    fz = z - iz
    ux = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0)
    uz = fz * fz * fz * (fz * (fz * 6.0 - 15.0) + 10.0)
    a = _hash2(ix, iz, seed, salt)
    b = _hash2(ix + 1, iz, seed, salt)
    c = _hash2(ix, iz + 1, seed, salt)
    d = _hash2(ix + 1, iz + 1, seed, salt)
    ab = a + (b - a) * ux
    cd = c + (d - c) * ux
    return ab + (cd - ab) * uz


def noise3(x, y, z, seed, salt):
    ix = math.floor(x)
    iy = math.floor(y)
    iz = math.floor(z)
    fx = x - ix
    fy = y - iy
    fz = z - iz
    ux = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0)
    uy = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0)
    uz = fz * fz * fz * (fz * (fz * 6.0 - 15.0) + 10.0)
    c000 = _hash3(ix, iy, iz, seed, salt)
    c100 = _hash3(ix + 1, iy, iz, seed, salt)
    c010 = _hash3(ix, iy + 1, iz, seed, salt)
    c110 = _hash3(ix + 1, iy + 1, iz, seed, salt)
    c001 = _hash3(ix, iy, iz + 1, seed, salt)
    c101 = _hash3(ix + 1, iy, iz + 1, seed, salt)
    c011 = _hash3(ix, iy + 1, iz + 1, seed, salt)
    c111 = _hash3(ix + 1, iy + 1, iz + 1, seed, salt)
    x00 = c000 + (c100 - c000) * ux
    x10 = c010 + (c110 - c010) * ux
    x01 = c001 + (c101 - c001) * ux
    x11 = c011 + (c111 - c011) * ux
    y0 = x00 + (x10 - x00) * uy
    y1 = x01 + (x11 - x01) * uy
    return y0 + (y1 - y0) * uz


def fbm2(x, z, octaves, seed, salt):
    total = 0.0
    amp = 1.0
    norm = 0.0
    freq = 1.0
    for octave in range(octaves):
        total += amp * noise2(x * freq, z * freq, seed, salt + octave)
        norm += amp
        amp *= 0.5
        freq *= 2.0
    return total / norm


def fbm3(x, y, z, octaves, seed, salt):
    total = 0.0
    amp = 1.0
    norm = 0.0
    freq = 1.0
    for octave in range(octaves):
        total += amp * noise3(x * freq, y * freq, z * freq, seed, salt + octave)
        norm += amp
        amp *= 0.5
        freq *= 2.0
    return total / norm


class Column:
    __slots__ = ("wx", "wz", "system", "mass", "bottom", "thickness", "top",
                 "vertical_phase", "top_soft")


def column_context(x, z, seed, height_scale):
    warp_x = (WARP_COARSE_M * (noise2(x / WARP_COARSE_SCALE_M, z / WARP_COARSE_SCALE_M, seed, 11) - 0.5) * 2.0 +
              WARP_FINE_M * (noise2(x / WARP_FINE_SCALE_M, z / WARP_FINE_SCALE_M, seed, 12) - 0.5) * 2.0)
    warp_z = (WARP_COARSE_M * (noise2(x / WARP_COARSE_SCALE_M + 17.31, z / WARP_COARSE_SCALE_M - 9.17, seed, 13) - 0.5) * 2.0 +
              WARP_FINE_M * (noise2(x / WARP_FINE_SCALE_M + 4.73, z / WARP_FINE_SCALE_M + 8.19, seed, 14) - 0.5) * 2.0)
    wx = x + warp_x
    wz = z + warp_z

    system_a = fbm2(wx / SYSTEM_SCALE_M, wz / SYSTEM_SCALE_M, 2, seed, 21)
    system_ridge = 1.0 - abs(2.0 * noise2(wx / SYSTEM_RIDGE_SCALE_M,
                                         wz / SYSTEM_RIDGE_SCALE_M, seed, 23) - 1.0)
    system_raw = 0.62 * system_a + 0.38 * system_ridge
    system = smoothstep(0.35, 0.85, system_raw)

    mass_a = fbm2(wx / MASS_SCALE_M, wz / MASS_SCALE_M, 2, seed, 31)
    mass_ridge = 1.0 - abs(2.0 * noise2(wx / MASS_RIDGE_SCALE_M,
                                        wz / MASS_RIDGE_SCALE_M, seed, 33) - 1.0)
    mass = smoothstep(0.22, 0.78, 0.60 * mass_a + 0.40 * mass_ridge)

    height_a = fbm2(wx / 28000.0, wz / 28000.0, 2, seed, 41)
    height_b = noise2(wx / 4000.0, wz / 4000.0, seed, 43)
    height_c = noise2(wx / 11000.0, wz / 11000.0, seed, 47)
    ca = 2.0 * height_a - 1.0
    cb = 2.0 * height_b - 1.0
    cc = 2.0 * height_c - 1.0
    bottom = (BOTTOM_BASE_M + BOTTOM_SYSTEM_M * ca +
              BOTTOM_FINE_M * cb + 180.0 * cc)
    thickness = (THICK_BASE_M + THICK_SYSTEM_M * ca +
                 THICK_FINE_M * cb + 350.0 * cc +
                 THICK_COVERAGE_M * system)
    thickness *= height_scale
    top_soft = 0.54 + 0.34 * noise2(wx / 7000.0, wz / 7000.0, seed, 45)

    col = Column()
    col.wx = wx
    col.wz = wz
    col.system = system
    col.mass = mass
    col.bottom = bottom
    col.thickness = max(200.0, thickness)
    col.top = bottom + col.thickness
    col.vertical_phase = 850.0 * height_a + 480.0 * height_b
    col.top_soft = top_soft
    return col


def vertical_window(y, col):
    v = (y - col.bottom) / col.thickness
    if v <= 0.0 or v >= 1.0:
        return 0.0
    lower = smoothstep(0.015, 0.095, v)
    upper = 1.0 - smoothstep(col.top_soft, 0.995, v)
    w = lower * upper
    return w if w > 0.0 else 0.0


def sample_voxel(y, col, seed):
    # Height-dependent horizontal displacement: the horizontal pattern at
    # the base, middle and top of a mass is different. This is the explicit
    # anti-extrusion term.
    yphase = y + col.vertical_phase
    shear_x = (SHEAR_AMPLITUDE_M *
               (noise2(yphase / SHEAR_VERTICAL_SCALE_M,
                       col.wz / SHEAR_HORIZONTAL_SCALE_M, seed, 61) - 0.5) * 2.0)
    shear_z = (SHEAR_AMPLITUDE_M *
               (noise2(yphase / SHEAR_VERTICAL_SCALE_M,
                       col.wx / SHEAR_HORIZONTAL_SCALE_M, seed, 62) - 0.5) * 2.0)
    m3 = fbm3((col.wx + shear_x) / MASS3_HORIZONTAL_SCALE_M,
              yphase / VERTICAL_SCALE_M,
              (col.wz + shear_z) / MASS3_HORIZONTAL_SCALE_M, 3, seed, 51)
    window = vertical_window(y, col)
    # True 3D occupancy: the 3D mass field, not a 2D mask extruded through a
    # per-column profile. Mass influence is retained for density modulation.
    vertical = window * (0.10 + 0.90 * m3)
    organisation = col.system * (0.20 + 0.80 * col.mass)
    field = organisation * vertical
    return {
        "mass3": m3,
        "vertical": vertical,
        "field": field,
    }


def occupancy_and_density(field_value, mass3, mass2, cut, half_width, density_gain):
    occupancy = smoothstep(cut - half_width, cut + half_width, field_value)
    if occupancy <= 0.0:
        return 0.0, 0.0
    density = occupancy * (0.65 + 0.35 * mass3) * (0.75 + 0.25 * mass2) * density_gain
    return occupancy, density


def field_bundle(x, y, z, seed, height_scale, cut, half_width, density_gain):
    col = column_context(x, z, seed, height_scale)
    vox = sample_voxel(y, col, seed)
    occ, den = occupancy_and_density(vox["field"], vox["mass3"], col.mass,
                                     cut, half_width, density_gain)
    return {
        "system": col.system,
        "mass": col.mass,
        "vertical": vox["vertical"],
        "occupancy": occ,
        "density": den,
        "bottom": col.bottom,
        "top": col.top,
        "raw_field": vox["field"],
    }


def _corr_1d(mask, n, lag):
    s = sx = sy = sxx = syy = 0.0
    count = 0
    for z in range(n):
        row = z * n
        for x in range(n - lag):
            a = mask[row + x]
            b = mask[row + x + lag]
            s += a * b
            sx += a
            sy += b
            sxx += a * a
            syy += b * b
            count += 1
    if count == 0:
        return 0.0
    num = s - sx * sy / count
    den = math.sqrt(max(0.0, (sxx - sx * sx / count) * (syy - sy * sy / count)))
    return num / den if den > 0.0 else 0.0


def autocorrelation(mask, n, step):
    distances = (3500.0, 7000.0, 14000.0, 20000.0, 40000.0)
    result = {}
    for distance in distances:
        lag = int(round(distance / step))
        if lag <= 0 or lag >= n:
            continue
        result[str(int(round(distance)))] = {
            "distance_m": lag * step,
            "corr": _corr_1d(mask, n, lag),
        }
    return result


def directional_spectrum(values, n, step):
    """1D DFT of row/column means, enough to expose a periodic step."""
    row_mean = []
    col_mean = []
    for z in range(n):
        row_mean.append(sum(values[z * n:(z + 1) * n]) / n)
    for x in range(n):
        col_mean.append(sum(values[z * n + x] for z in range(n)) / n)
    out = {}
    for name, series in (("row_mean_z", row_mean), ("col_mean_x", col_mean)):
        best = None
        peaks = []
        for k in range(1, min(n // 2, 64) + 1):
            re = im = 0.0
            phase = 2.0 * math.pi * k / n
            for i, v in enumerate(series):
                re += v * math.cos(phase * i)
                im -= v * math.sin(phase * i)
            amp = math.hypot(re, im) / n
            peaks.append((amp, k, n * step / k))
        peaks.sort(reverse=True)
        best = peaks[0] if peaks else None
        out[name] = {
            "dominant_wavelength_m": best[2] if best else 0.0,
            "dominant_amplitude": best[0] if best else 0.0,
            "top5": [
                {"wavelength_m": p[2], "amplitude": p[0]}
                for p in peaks[:5]
            ],
        }
    return out


def _histogram(values, bins):
    if not values:
        return {}
    lo = min(values)
    hi = max(values)
    if hi <= lo:
        return {f"{lo:.1f}": len(values)}
    out = {}
    for i in range(bins):
        a = lo + (hi - lo) * i / bins
        b = lo + (hi - lo) * (i + 1) / bins
        count = sum(1 for v in values if (v >= a and (v < b or i == bins - 1)))
        out[f"{a:.1f}..{b:.1f}"] = count
    return out


def _neighbour_corr(columns, nx, nz, key):
    vals = []
    for z in range(nz):
        for x in range(nx - 1):
            a = columns[z * nx + x]
            b = columns[z * nx + x + 1]
            if a["occupied"] and b["occupied"]:
                vals.append((a[key], b[key]))
    if len(vals) < 3:
        return 0.0
    ma = sum(v[0] for v in vals) / len(vals)
    mb = sum(v[1] for v in vals) / len(vals)
    num = sum((a - ma) * (b - mb) for a, b in vals)
    da = math.sqrt(sum((a - ma) ** 2 for a, _ in vals))
    db = math.sqrt(sum((b - mb) ** 2 for _, b in vals))
    return num / (da * db) if da > 0.0 and db > 0.0 else 0.0


def _vertical_stats(profiles, dy, height_scale):
    occupied = [p for p in profiles if p["occupied"]]
    if not occupied:
        return {"occupied_columns": 0}
    thickness = [p["thickness_m"] for p in occupied]
    tops = [p["top_m"] for p in occupied]
    bottoms = [p["bottom_m"] for p in occupied]
    centroids = [p["centroid_m"] for p in occupied]
    intervals = [p["intervals"] for p in occupied]
    max_density = [p["max_density"] for p in occupied]
    signature_counts = {}
    for p in occupied:
        signature = tuple(p["signature"])
        signature_counts[signature] = signature_counts.get(signature, 0) + 1
    return {
        "occupied_columns": len(occupied),
        "thickness_m": {
            "p50": percentile(thickness, 50.0),
            "p90": percentile(thickness, 90.0),
            "p95": percentile(thickness, 95.0),
            "mean": sum(thickness) / len(thickness),
            "std": (sum((v - sum(thickness) / len(thickness)) ** 2 for v in thickness) / len(thickness)) ** 0.5,
            "min": min(thickness),
            "max": max(thickness),
        },
        "top_m": {
            "p50": percentile(tops, 50.0),
            "p90": percentile(tops, 90.0),
            "p95": percentile(tops, 95.0),
            "std": (sum((v - sum(tops) / len(tops)) ** 2 for v in tops) / len(tops)) ** 0.5,
            "min": min(tops),
            "max": max(tops),
        },
        "bottom_m": {
            "p50": percentile(bottoms, 50.0),
            "p90": percentile(bottoms, 90.0),
            "p95": percentile(bottoms, 95.0),
            "std": (sum((v - sum(bottoms) / len(bottoms)) ** 2 for v in bottoms) / len(bottoms)) ** 0.5,
            "min": min(bottoms),
            "max": max(bottoms),
        },
        "centroid_m": {
            "p50": percentile(centroids, 50.0),
            "p90": percentile(centroids, 90.0),
            "p95": percentile(centroids, 95.0),
            "std": (sum((v - sum(centroids) / len(centroids)) ** 2 for v in centroids) / len(centroids)) ** 0.5,
        },
        "intervals_per_column": {
            "p50": percentile(intervals, 50.0),
            "p90": percentile(intervals, 90.0),
            "p95": percentile(intervals, 95.0),
            "mean": sum(intervals) / len(intervals),
            "fraction_gt1": sum(1 for v in intervals if v > 1) / len(intervals),
        },
        "max_density": {
            "p50": percentile(max_density, 50.0),
            "p90": percentile(max_density, 90.0),
            "mean": sum(max_density) / len(max_density),
        },
        "profile_signatures": {
            "unique": len(signature_counts),
            "occupied": len(occupied),
            "unique_fraction": len(signature_counts) / len(occupied),
            "most_common_count": max(signature_counts.values()),
        },
        "height_scale": height_scale,
    }


def _render(values, nx, nz, path, vmin=0.0, vmax=1.0, width=None):
    rows = []
    for z in range(nz):
        row = bytearray()
        for x in range(nx):
            v = values[z * nx + x]
            t = (v - vmin) / (vmax - vmin) if vmax > vmin else 0.0
            row.extend(color_map(t))
        rows.append(row)
    buf = rgb_rows_to_buffer(rows)
    if width is not None and width != nx:
        height = max(1, round(width * nz / nx))
        buf = resample_nearest(buf, nx, nz, width, height)
        png_write(path, width, height, buf)
    else:
        png_write(path, nx, nz, buf)


def _render_aspect(values, nx, ny, x_extent, y_extent, path, width=800):
    dst_w, dst_h = world_aspect_size(x_extent, y_extent, width)
    rows = []
    for j in range(ny):
        row = bytearray()
        for i in range(nx):
            row.extend(color_map(values[j * nx + i]))
        rows.append(row)
    buf = rgb_rows_to_buffer(rows)
    buf = resample_nearest(buf, nx, ny, dst_w, dst_h)
    png_write(path, dst_w, dst_h, buf)


def evaluate(args):
    started = time.perf_counter()
    seed = args.seed
    size = float(args.size)
    nx = int(args.res_xz)
    nz = nx
    ny = int(args.res_y)
    x0 = float(args.center_x) - size / 2.0
    z0 = float(args.center_z) - size / 2.0
    dx = size / nx
    dz = size / nz
    y0 = float(args.y_min)
    y1 = float(args.y_max)
    dy = (y1 - y0) / ny
    cut = float(args.cut)
    half_width = float(args.half_width)
    height_scale = float(args.height_scale)
    density_gain = float(args.density_gain)
    args.out.mkdir(parents=True, exist_ok=True)

    source_sha = sha256_file(Path(__file__))
    total_voxels = nx * ny * nz
    occ_all = array.array("f", [0.0]) * total_voxels
    den_all = array.array("f", [0.0]) * total_voxels
    sys_all = array.array("f", [0.0]) * total_voxels if args.dump_volume else None
    mass_all = array.array("f", [0.0]) * total_voxels if args.dump_volume else None
    vert_all = array.array("f", [0.0]) * total_voxels if args.dump_volume else None
    column_max = array.array("f", [0.0]) * (nx * nz)
    column_top = array.array("f", [0.0]) * (nx * nz)
    column_bottom = array.array("f", [0.0]) * (nx * nz)
    profiles = []

    for iz in range(nz):
        z = z0 + (iz + 0.5) * dz
        for ix in range(nx):
            x = x0 + (ix + 0.5) * dx
            col = column_context(x, z, seed, height_scale)
            profile = [0.0] * ny
            dens_profile = [0.0] * ny
            best = 0.0
            best_y = y0
            bottom_y = None
            top_y = None
            weighted = 0.0
            weight_sum = 0.0
            max_density = 0.0
            for iy in range(ny):
                y = y0 + (iy + 0.5) * dy
                vox = sample_voxel(y, col, seed)
                occ, den = occupancy_and_density(vox["field"], vox["mass3"],
                                                 col.mass, cut, half_width,
                                                 density_gain)
                index = (iz * ny + iy) * nx + ix
                occ_all[index] = occ
                den_all[index] = den
                if args.dump_volume:
                    sys_all[index] = col.system
                    mass_all[index] = col.mass
                    vert_all[index] = vox["vertical"]
                profile[iy] = occ
                dens_profile[iy] = den
                if occ > best:
                    best = occ
                    best_y = y
                if occ > OCCUPANCY_THRESHOLD:
                    bottom_y = y if bottom_y is None else bottom_y
                    top_y = y
                if den > 0.0:
                    weighted += den * y
                    weight_sum += den
                    if den > max_density:
                        max_density = den

            cidx = iz * nx + ix
            column_max[cidx] = best
            column_top[cidx] = top_y if top_y is not None else 0.0
            column_bottom[cidx] = bottom_y if bottom_y is not None else 0.0
            occupied = best > OCCUPANCY_THRESHOLD
            intervals = 0
            inside = False
            for iy in range(ny):
                on = profile[iy] > OCCUPANCY_THRESHOLD
                if on and not inside:
                    intervals += 1
                inside = on
            signature = []
            for bin_index in range(12):
                a = int(bin_index * ny / 12)
                b = int((bin_index + 1) * ny / 12)
                signature.append(int(round(sum(profile[a:b]) * 4.0 / max(1, b - a))))
            profiles.append({
                "x": x,
                "z": z,
                "occupied": occupied,
                "column_max": best,
                "best_y_m": best_y,
                "top_m": top_y if top_y is not None else 0.0,
                "bottom_m": bottom_y if bottom_y is not None else 0.0,
                "thickness_m": ((top_y - bottom_y + dy) if top_y is not None else 0.0),
                "centroid_m": (weighted / weight_sum) if weight_sum > 0.0 else 0.0,
                "intervals": intervals,
                "max_density": max_density,
                "signature": signature,
            })

    elapsed = time.perf_counter() - started
    mask = [1 if v > OCCUPANCY_THRESHOLD else 0 for v in column_max]
    occupied_columns = sum(mask) / (nx * nz)
    occ_mask = [1 if v > OCCUPANCY_THRESHOLD else 0 for v in occ_all]
    occupied_voxels = sum(occ_mask) / total_voxels

    comp2d = component_stats_2d(mask, nx, nz, dx * dz)
    comp3d = component_stats_3d(occ_mask, nx, ny, nz, dx * dy * dz)
    vert = _vertical_stats(profiles, dy, height_scale)
    col_dicts = profiles
    vert["neighbour_corr_top_x"] = _neighbour_corr(col_dicts, nx, nz, "top_m")
    vert["neighbour_corr_thickness_x"] = _neighbour_corr(col_dicts, nx, nz, "thickness_m")
    # z-direction pairs
    zvals = []
    for iz in range(nz - 1):
        for ix in range(nx):
            a = col_dicts[iz * nx + ix]
            b = col_dicts[(iz + 1) * nx + ix]
            if a["occupied"] and b["occupied"]:
                zvals.append((a["top_m"], b["top_m"]))
    if len(zvals) >= 3:
        ma = sum(v[0] for v in zvals) / len(zvals)
        mb = sum(v[1] for v in zvals) / len(zvals)
        da = math.sqrt(sum((v[0] - ma) ** 2 for v in zvals))
        db = math.sqrt(sum((v[1] - mb) ** 2 for v in zvals))
        vert["neighbour_corr_top_z"] = (sum((a - ma) * (b - mb) for a, b in zvals) /
                                        (da * db)) if da > 0 and db > 0 else 0.0
    else:
        vert["neighbour_corr_top_z"] = 0.0

    threshold_stats = {}
    for threshold in (0.05, 0.1, 0.2, 0.4, 0.5):
        threshold_stats[str(threshold)] = {
            "columns": sum(1 for v in column_max if v > threshold) / (nx * nz),
            "voxels": sum(1 for v in occ_all if v > threshold) / total_voxels,
        }

    stats = {
        "format": VERSION,
        "seed": seed,
        "weather": args.weather,
        "coverage": args.coverage,
        "height_scale": height_scale,
        "density_gain": density_gain,
        "grid": [nx, ny, nz],
        "world_origin": [x0, y0, z0],
        "cell_size_m": [dx, dy, dz],
        "extent_m": [size, y1 - y0, size],
        "cut": cut,
        "half_width": half_width,
        "occupied_columns": occupied_columns,
        "occupied_voxels": occupied_voxels,
        "component_2d_xzmax": comp2d,
        "component_3d": comp3d,
        "vertical": vert,
        "autocorrelation_xzmax": autocorrelation(mask, nx, dx),
        "spectrum_xzmax": directional_spectrum(column_max, nx, dx),
        "thresholds": threshold_stats,
        "column_max": {
            "p50": percentile(list(column_max), 50.0),
            "p90": percentile(list(column_max), 90.0),
            "p95": percentile(list(column_max), 95.0),
            "max": max(column_max) if column_max else 0.0,
        },
        "runtime_seconds": elapsed,
        "total_samples": total_voxels,
        "samples_per_second": total_voxels / elapsed if elapsed > 0 else 0.0,
        "source_sha256": source_sha,
        "algorithm": "HCS1: domain-warped system + billow mass + variable height envelope + 3D mass",
        "parameters": {
            "system_scale_m": SYSTEM_SCALE_M,
            "system_ridge_scale_m": SYSTEM_RIDGE_SCALE_M,
            "mass_scale_m": MASS_SCALE_M,
            "mass_ridge_scale_m": MASS_RIDGE_SCALE_M,
            "mass3_horizontal_scale_m": MASS3_HORIZONTAL_SCALE_M,
            "vertical_scale_m": VERTICAL_SCALE_M,
            "shear_amplitude_m": SHEAR_AMPLITUDE_M,
            "shear_vertical_scale_m": SHEAR_VERTICAL_SCALE_M,
            "shear_horizontal_scale_m": SHEAR_HORIZONTAL_SCALE_M,
            "warp_coarse_m": WARP_COARSE_M,
            "warp_coarse_scale_m": WARP_COARSE_SCALE_M,
            "warp_fine_m": WARP_FINE_M,
            "warp_fine_scale_m": WARP_FINE_SCALE_M,
            "bottom_base_m": BOTTOM_BASE_M,
            "bottom_system_m": BOTTOM_SYSTEM_M,
            "bottom_fine_m": BOTTOM_FINE_M,
            "thick_base_m": THICK_BASE_M,
            "thick_system_m": THICK_SYSTEM_M,
            "thick_fine_m": THICK_FINE_M,
            "thick_coverage_m": THICK_COVERAGE_M,
            "occupancy_threshold": OCCUPANCY_THRESHOLD,
        },
    }

    with open(args.out / f"stats_s{seed}.json", "w", encoding="utf-8") as handle:
        json.dump(stats, handle, indent=2)
        handle.write("\n")

    if args.dump_xzmax:
        meta = {
            "format": VERSION,
            "field": "column_max_occupancy",
            "seed": seed,
            "grid": [nx, 1, nz],
            "world_origin": [x0, 0.0, z0],
            "cell_size_m": [dx, 1.0, dz],
            "axes": ["x", "y", "z"],
            "index_order": "x fastest, then z (single y layer)",
            "algorithm": stats["algorithm"],
            "parameters": stats["parameters"],
            "weather": args.weather,
            "coverage": args.coverage,
            "cut": cut,
            "source_sha256": source_sha,
            "provenance": "proto_r02_systems.py --dump-xzmax",
        }
        write_float32_field(args.out / f"xzmax_s{seed}.bin", column_max, meta)

    if args.dump_volume:
        base_meta = {
            "format": VERSION,
            "seed": seed,
            "grid": [nx, ny, nz],
            "world_origin": [x0, y0, z0],
            "cell_size_m": [dx, dy, dz],
            "axes": ["x", "y", "z"],
            "index_order": "x fastest, then y, then z",
            "algorithm": stats["algorithm"],
            "parameters": stats["parameters"],
            "weather": args.weather,
            "coverage": args.coverage,
            "cut": cut,
            "source_sha256": source_sha,
            "provenance": "proto_r02_systems.py --dump-volume",
        }
        for name, values, channel in (
                ("system", sys_all, "system influence [0,1]"),
                ("mass", mass_all, "mass influence [0,1]"),
                ("vertical", vert_all, "vertical structure [0,1]"),
                ("occupancy", occ_all, "occupancy [0,1]"),
                ("density", den_all, "normalised density [0,1]")):
            meta = dict(base_meta)
            meta["field"] = channel
            write_float32_field(args.out / f"{name}_s{seed}.bin", values, meta)

    if args.render:
        _render(list(column_max), nx, nz, args.out / f"xzmax_s{seed}.png")
        _render([1.0 if v > OCCUPANCY_THRESHOLD else 0.0 for v in column_max],
                nx, nz, args.out / f"xzmax_mask_s{seed}.png")
        # XZ slices at several heights.
        for height in (1800.0, 2600.0, 3400.0):
            if height < y0 or height >= y1:
                continue
            slice_occ = []
            slice_sys = []
            slice_mass = []
            slice_vert = []
            for iz in range(nz):
                z = z0 + (iz + 0.5) * dz
                for ix in range(nx):
                    x = x0 + (ix + 0.5) * dx
                    col = column_context(x, z, seed, height_scale)
                    vox = sample_voxel(height, col, seed)
                    occ, _ = occupancy_and_density(vox["field"], vox["mass3"], col.mass,
                                                  cut, half_width, density_gain)
                    slice_occ.append(occ)
                    slice_sys.append(col.system)
                    slice_mass.append(col.mass)
                    slice_vert.append(vox["vertical"])
            _render(slice_occ, nx, nz, args.out / f"xz_y{int(height)}_occ_s{seed}.png")
            if height == 2600.0:
                _render(slice_sys, nx, nz, args.out / f"xz_y{int(height)}_system_s{seed}.png")
                _render(slice_mass, nx, nz, args.out / f"xz_y{int(height)}_mass_s{seed}.png")
                _render(slice_vert, nx, nz, args.out / f"xz_y{int(height)}_vertical_s{seed}.png")
        # XY and YZ through the domain centre, aspect corrected.
        z_center = args.center_z
        x_center = args.center_x
        xy = []
        yz = []
        for iy in range(ny):
            y = y0 + (iy + 0.5) * dy
            for ix in range(nx):
                x = x0 + (ix + 0.5) * dx
                col = column_context(x, z_center, seed, height_scale)
                vox = sample_voxel(y, col, seed)
                occ, _ = occupancy_and_density(vox["field"], vox["mass3"], col.mass,
                                              cut, half_width, density_gain)
                xy.append(occ)
            for iz in range(nz):
                z = z0 + (iz + 0.5) * dz
                col = column_context(x_center, z, seed, height_scale)
                vox = sample_voxel(y, col, seed)
                occ, _ = occupancy_and_density(vox["field"], vox["mass3"], col.mass,
                                              cut, half_width, density_gain)
                yz.append(occ)
        _render_aspect(xy, nx, ny, size, y1 - y0,
                       args.out / f"xy_s{seed}.png", width=min(1000, nx * 6))
        _render_aspect(yz, nz, ny, size, y1 - y0,
                       args.out / f"yz_s{seed}.png", width=min(1000, nz * 6))
        # Vertical profile heatmap: 64 columns along x at z = centre.
        prof_w = min(96, nx)
        prof_values = []
        for ix in range(prof_w):
            x = x0 + (ix + 0.5) * size / prof_w
            col = column_context(x, z_center, seed, height_scale)
            for iy in range(ny):
                y = y0 + (iy + 0.5) * dy
                vox = sample_voxel(y, col, seed)
                occ, _ = occupancy_and_density(vox["field"], vox["mass3"], col.mass,
                                              cut, half_width, density_gain)
                prof_values.append(occ)
        _render(prof_values, prof_w, ny, args.out / f"profiles_s{seed}.png")
        # Cloud-top hillshade preview (2.5D) using column_top.
        top = list(column_top)
        top_min = min(top)
        top_max = max(top)
        rows = []
        for iz in range(nz):
            row = bytearray()
            for ix in range(nx):
                h = top[iz * nx + ix]
                t = (h - top_min) / (top_max - top_min) if top_max > top_min else 0.0
                if h <= 0.0:
                    row.extend(color_map(0.0))
                    continue
                left = top[iz * nx + max(0, ix - 1)]
                down = top[max(0, iz - 1) * nx + ix]
                shade = 0.55 + 0.45 * clamp01(0.5 + (h - left + h - down) / 1500.0)
                c = color_map(0.35 + 0.65 * t)
                row.extend(tuple(int(c[k] * shade) for k in range(3)))
            rows.append(row)
        png_write(args.out / f"top_surface_s{seed}.png", nx, nz, rgb_rows_to_buffer(rows))

    print(json.dumps({
        "seed": seed,
        "grid": [nx, ny, nz],
        "occupied_columns": occupied_columns,
        "occupied_voxels": occupied_voxels,
        "components_3d": comp3d["count"],
        "largest_3d_fraction": comp3d["largest_fraction"],
        "thickness_p50": vert.get("thickness_m", {}).get("p50", 0.0),
        "thickness_p90": vert.get("thickness_m", {}).get("p90", 0.0),
        "runtime_seconds": elapsed,
        "samples_per_second": stats["samples_per_second"],
    }, indent=2))
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=1337)
    parser.add_argument("--size", type=float, default=16000.0)
    parser.add_argument("--res-xz", type=int, default=96)
    parser.add_argument("--res-y", type=int, default=32)
    parser.add_argument("--y-min", type=float, default=800.0)
    parser.add_argument("--y-max", type=float, default=4800.0)
    parser.add_argument("--center-x", type=float, default=0.0)
    parser.add_argument("--center-z", type=float, default=0.0)
    parser.add_argument("--cut", type=float, default=0.18)
    parser.add_argument("--half-width", type=float, default=0.035)
    parser.add_argument("--weather", default="custom")
    parser.add_argument("--coverage", type=float, default=0.0)
    parser.add_argument("--height-scale", type=float, default=1.0)
    parser.add_argument("--density-gain", type=float, default=1.0)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--dump-volume", action="store_true")
    parser.add_argument("--dump-xzmax", action="store_true")
    parser.add_argument("--render", action="store_true")
    args = parser.parse_args()
    return evaluate(args)


if __name__ == "__main__":
    sys.exit(main())
