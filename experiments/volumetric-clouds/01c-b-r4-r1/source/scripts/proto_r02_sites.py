#!/usr/bin/env python3
"""R02 HCS2: hierarchical system field + two incommensurate site grids.

This is the second CPU architecture probe for R02. It keeps the Level-1
large-scale system field from HCS1, then represents Level-2 masses as
anisotropic 3D sites on two rotated, jittered grids with different cell
sizes. Domain warping is applied before grid lookup, so the generator grids
are not axis-aligned in world space. Level-3 vertical structure comes from
per-site centre height, asymmetric vertical radii and a smooth 3D warp; there
is no global per-column extrusion profile.

CPU research only. No HLSL, no GPU, no runtime integration.
"""

import argparse
import array
import json
import math
import sys
import time
from pathlib import Path

from r02_diagnostics import (
    component_stats_2d,
    component_stats_3d,
    percentile,
    rgb_rows_to_buffer,
    png_write,
    resample_nearest,
    sha256_file,
    world_aspect_size,
    write_float32_field,
)
from proto_r02_systems import (
    _neighbour_corr,
    _render,
    _render_aspect,
    _vertical_stats,
    autocorrelation,
    clamp01,
    directional_spectrum,
    fbm2,
    noise2,
    smoothstep,
)

MASK32 = 0xFFFFFFFF
VERSION = "OGSR_V2_3h_A03b_R02_HCS2"
OCCUPANCY_THRESHOLD = 0.2


class Site:
    __slots__ = ("cx", "cz", "cy", "rx", "rz", "ryt", "ryb", "yaw", "box",
                 "w1", "w2", "w3")


_SITE_CACHE = {}


def _hash(a, b, seed, salt):
    h = (a * 374761393 + b * 668265263 + seed * 2246822519 + salt * 3266489917) & MASK32
    h ^= h >> 15
    h = (h * 0x2C1B3C6D) & MASK32
    h ^= h >> 12
    h = (h * 0x297A2D39) & MASK32
    h ^= h >> 15
    return h / 4294967296.0


class Rng:
    __slots__ = ("v",)

    def __init__(self, value):
        self.v = value & MASK32

    def next(self):
        self.v ^= (self.v << 13) & MASK32
        self.v ^= self.v >> 17
        self.v ^= (self.v << 5) & MASK32
        self.v &= MASK32
        return self.v / 4294967296.0


def _site(grid_id, cell, ix, iz, seed):
    key = (grid_id, cell, ix, iz, seed)
    site = _SITE_CACHE.get(key)
    if site is not None:
        return site
    rng = Rng(int(_hash(ix, iz, seed, 101 + grid_id) * 4294967296.0))
    site = Site()
    site.cx = (ix + 0.15 + 0.70 * rng.next()) * cell
    site.cz = (iz + 0.15 + 0.70 * rng.next()) * cell
    site.cy = 1550.0 + 2300.0 * rng.next()
    base = cell * (0.30 + 0.18 * rng.next())
    site.rx = base * (0.80 + 0.40 * rng.next())
    site.rz = base * (0.80 + 0.40 * rng.next())
    site.ryt = 420.0 + 1000.0 * rng.next()
    site.ryb = 360.0 + 900.0 * rng.next()
    site.yaw = rng.next() * 2.0 * math.pi
    site.box = 0.10 + 0.45 * rng.next()
    site.w1 = rng.next() * 2.0 - 1.0
    site.w2 = rng.next() * 2.0 - 1.0
    site.w3 = rng.next() * 2.0 - 1.0
    _SITE_CACHE[key] = site
    return site


def _grid_influence(u, v, y, cell, grid_id, seed):
    ix = math.floor(u / cell)
    iz = math.floor(v / cell)
    best = -1e9
    for cz in (iz - 1, iz, iz + 1):
        for cx in (ix - 1, ix, ix + 1):
            s = _site(grid_id, cell, cx, cz, seed)
            dx = u - s.cx
            dz = v - s.cz
            c = math.cos(s.yaw)
            sn = math.sin(s.yaw)
            rx = (dx * c - dz * sn) / s.rx
            rz = (dx * sn + dz * c) / s.rz
            dy = y - s.cy
            ry = s.ryt if dy > 0.0 else s.ryb
            zy = dy / ry
            e = math.sqrt(rx * rx + rz * rz + zy * zy)
            b = max(abs(rx), abs(rz), abs(zy))
            d = e + (b - e) * s.box
            d += s.w1 * 0.10 * (rx * rz)
            d += s.w2 * 0.07 * (zy * rz)
            d += s.w3 * 0.05 * (rx * zy)
            f = 1.0 - d
            if f > best:
                best = f
    return best


class Column:
    __slots__ = ("wx", "wz", "system", "u_a", "v_a", "u_b", "v_b")


def column_context(x, z, seed):
    wx = (x + 3000.0 * (noise2(x / 30000.0, z / 30000.0, seed, 11) - 0.5) * 2.0 +
          900.0 * (noise2(x / 9000.0, z / 9000.0, seed, 12) - 0.5) * 2.0)
    wz = (z + 3000.0 * (noise2(x / 30000.0 + 13.7, z / 30000.0 - 4.1, seed, 13) - 0.5) * 2.0 +
          900.0 * (noise2(x / 9000.0 + 5.3, z / 9000.0 + 9.7, seed, 14) - 0.5) * 2.0)
    system = fbm2(wx / 48000.0, wz / 48000.0, 2, seed, 21)
    system = smoothstep(0.28, 0.78, 0.72 * system + 0.28 * (1.0 - abs(
        2.0 * noise2(wx / 30000.0, wz / 30000.0, seed, 23) - 1.0)))
    col = Column()
    col.wx = wx
    col.wz = wz
    col.system = system
    # Grid A: 5.3 km axis aligned. Grid B: 7.3 km rotated 31 degrees.
    col.u_a = wx
    col.v_a = wz
    ang = math.radians(31.0)
    col.u_b = wx * math.cos(ang) + wz * math.sin(ang)
    col.v_b = -wx * math.sin(ang) + wz * math.cos(ang)
    return col


def sample_site(y, col, seed):
    f_a = _grid_influence(col.u_a, col.v_a, y, 5300.0, 1, seed)
    f_b = _grid_influence(col.u_b, col.v_b, y, 7300.0, 2, seed)
    f = max(f_a, f_b)
    strength = 0.35 + 0.65 * col.system
    return f * strength


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
    args.out.mkdir(parents=True, exist_ok=True)
    source_sha = sha256_file(Path(__file__))
    total = nx * ny * nz
    occ_all = array.array("f", [0.0]) * total
    den_all = array.array("f", [0.0]) * total
    column_max = array.array("f", [0.0]) * (nx * nz)
    column_top = array.array("f", [0.0]) * (nx * nz)
    column_bottom = array.array("f", [0.0]) * (nx * nz)
    profiles = []

    for iz in range(nz):
        z = z0 + (iz + 0.5) * dz
        for ix in range(nx):
            x = x0 + (ix + 0.5) * dx
            col = column_context(x, z, seed)
            profile = [0.0] * ny
            best = 0.0
            bottom_y = None
            top_y = None
            weighted = 0.0
            weight_sum = 0.0
            max_density = 0.0
            for iy in range(ny):
                y = y0 + (iy + 0.5) * dy
                raw = sample_site(y, col, seed)
                occ = smoothstep(cut - half_width, cut + half_width, raw)
                den = occ * (0.55 + 0.45 * clamp01(raw)) if occ > 0.0 else 0.0
                index = (iz * ny + iy) * nx + ix
                occ_all[index] = occ
                den_all[index] = den
                profile[iy] = occ
                if occ > best:
                    best = occ
                if occ > OCCUPANCY_THRESHOLD:
                    bottom_y = y if bottom_y is None else bottom_y
                    top_y = y
                if den > 0.0:
                    weighted += den * y
                    weight_sum += den
                    max_density = max(max_density, den)
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
                "x": x, "z": z, "occupied": occupied, "column_max": best,
                "top_m": top_y if top_y is not None else 0.0,
                "bottom_m": bottom_y if bottom_y is not None else 0.0,
                "thickness_m": ((top_y - bottom_y + dy) if top_y is not None else 0.0),
                "centroid_m": (weighted / weight_sum) if weight_sum > 0.0 else 0.0,
                "intervals": intervals, "max_density": max_density,
                "signature": signature,
            })

    elapsed = time.perf_counter() - started
    mask = [1 if v > OCCUPANCY_THRESHOLD else 0 for v in column_max]
    occ_mask = [1 if v > OCCUPANCY_THRESHOLD else 0 for v in occ_all]
    vertical = _vertical_stats(profiles, dy, 1.0)
    vertical["neighbour_corr_top_x"] = _neighbour_corr(profiles, nx, nz, "top_m")
    vertical["neighbour_corr_thickness_x"] = _neighbour_corr(profiles, nx, nz, "thickness_m")
    zvals = []
    for iz in range(nz - 1):
        for ix in range(nx):
            a = profiles[iz * nx + ix]
            b = profiles[(iz + 1) * nx + ix]
            if a["occupied"] and b["occupied"]:
                zvals.append((a["top_m"], b["top_m"]))
    if len(zvals) >= 3:
        ma = sum(v[0] for v in zvals) / len(zvals)
        mb = sum(v[1] for v in zvals) / len(zvals)
        da = math.sqrt(sum((v[0] - ma) ** 2 for v in zvals))
        db = math.sqrt(sum((v[1] - mb) ** 2 for v in zvals))
        vertical["neighbour_corr_top_z"] = (sum((a - ma) * (b - mb) for a, b in zvals) /
                                            (da * db)) if da > 0 and db > 0 else 0.0
    else:
        vertical["neighbour_corr_top_z"] = 0.0

    stats = {
        "format": VERSION,
        "seed": args.seed,
        "grid": [nx, ny, nz],
        "world_origin": [x0, y0, z0],
        "cell_size_m": [dx, dy, dz],
        "extent_m": [size, y1 - y0, size],
        "cut": cut,
        "half_width": half_width,
        "occupied_columns": sum(mask) / (nx * nz),
        "occupied_voxels": sum(occ_mask) / total,
        "component_2d_xzmax": component_stats_2d(mask, nx, nz, dx * dz),
        "component_3d": component_stats_3d(occ_mask, nx, ny, nz, dx * dy * dz),
        "vertical": vertical,
        "autocorrelation_xzmax": autocorrelation(mask, nx, dx),
        "spectrum_xzmax": directional_spectrum(column_max, nx, dx),
        "runtime_seconds": elapsed,
        "total_samples": total,
        "samples_per_second": total / elapsed if elapsed > 0.0 else 0.0,
        "source_sha256": source_sha,
        "algorithm": "HCS2: system field + two incommensurate rotated 3D site grids",
        "parameters": {
            "grid_a_cell_m": 5300.0,
            "grid_b_cell_m": 7300.0,
            "grid_b_rotation_deg": 31.0,
            "site_y_range_m": [1550.0, 3850.0],
            "site_vertical_radii_m": [360.0, 1420.0],
            "warp_coarse_m": 3000.0,
            "warp_fine_m": 900.0,
            "system_scale_m": 48000.0,
            "system_ridge_scale_m": 30000.0,
            "occupancy_threshold": OCCUPANCY_THRESHOLD,
        },
    }
    with open(args.out / f"stats_s{args.seed}.json", "w", encoding="utf-8") as handle:
        json.dump(stats, handle, indent=2)
        handle.write("\n")

    if args.dump_xzmax:
        meta = {
            "format": VERSION, "field": "column_max_occupancy", "seed": args.seed,
            "grid": [nx, 1, nz], "world_origin": [x0, 0.0, z0],
            "cell_size_m": [dx, 1.0, dz], "axes": ["x", "y", "z"],
            "index_order": "x fastest, then z (single y layer)",
            "algorithm": stats["algorithm"], "parameters": stats["parameters"],
            "source_sha256": source_sha,
            "provenance": "proto_r02_sites.py --dump-xzmax",
        }
        write_float32_field(args.out / f"xzmax_s{args.seed}.bin", column_max, meta)

    if args.render:
        _render(list(column_max), nx, nz, args.out / f"xzmax_s{args.seed}.png")
        _render([1.0 if v > OCCUPANCY_THRESHOLD else 0.0 for v in column_max],
                nx, nz, args.out / f"xzmax_mask_s{args.seed}.png")
        for height in (1800.0, 2600.0, 3400.0):
            if height < y0 or height >= y1:
                continue
            values = []
            for iz in range(nz):
                z = z0 + (iz + 0.5) * dz
                for ix in range(nx):
                    x = x0 + (ix + 0.5) * dx
                    col = column_context(x, z, seed)
                    raw = sample_site(height, col, seed)
                    values.append(smoothstep(cut - half_width, cut + half_width, raw))
            _render(values, nx, nz, args.out / f"xz_y{int(height)}_occ_s{args.seed}.png")
        zc = args.center_z
        xc = args.center_x
        xy = []
        yz = []
        for iy in range(ny):
            y = y0 + (iy + 0.5) * dy
            for ix in range(nx):
                x = x0 + (ix + 0.5) * dx
                col = column_context(x, zc, seed)
                raw = sample_site(y, col, seed)
                xy.append(smoothstep(cut - half_width, cut + half_width, raw))
            for iz in range(nz):
                z = z0 + (iz + 0.5) * dz
                col = column_context(xc, z, seed)
                raw = sample_site(y, col, seed)
                yz.append(smoothstep(cut - half_width, cut + half_width, raw))
        _render_aspect(xy, nx, ny, size, y1 - y0, args.out / f"xy_s{args.seed}.png", width=800)
        _render_aspect(yz, nz, ny, size, y1 - y0, args.out / f"yz_s{args.seed}.png", width=800)
        prof_w = min(96, nx)
        prof = []
        for ix in range(prof_w):
            x = x0 + (ix + 0.5) * size / prof_w
            col = column_context(x, zc, seed)
            for iy in range(ny):
                y = y0 + (iy + 0.5) * dy
                raw = sample_site(y, col, seed)
                prof.append(smoothstep(cut - half_width, cut + half_width, raw))
        _render(prof, prof_w, ny, args.out / f"profiles_s{args.seed}.png")

    print(json.dumps({
        "seed": args.seed, "grid": [nx, ny, nz],
        "occupied_columns": stats["occupied_columns"],
        "occupied_voxels": stats["occupied_voxels"],
        "components_3d": stats["component_3d"]["count"],
        "largest_3d_fraction": stats["component_3d"]["largest_fraction"],
        "thickness_p50": stats["vertical"].get("thickness_m", {}).get("p50", 0.0),
        "thickness_p90": stats["vertical"].get("thickness_m", {}).get("p90", 0.0),
        "runtime_seconds": elapsed,
        "samples_per_second": stats["samples_per_second"],
    }, indent=2))
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--seed", type=int, default=1337)
    parser.add_argument("--size", type=float, default=16000.0)
    parser.add_argument("--res-xz", type=int, default=96)
    parser.add_argument("--res-y", type=int, default=40)
    parser.add_argument("--y-min", type=float, default=400.0)
    parser.add_argument("--y-max", type=float, default=5800.0)
    parser.add_argument("--center-x", type=float, default=0.0)
    parser.add_argument("--center-z", type=float, default=0.0)
    parser.add_argument("--cut", type=float, default=0.3)
    parser.add_argument("--half-width", type=float, default=0.035)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--dump-xzmax", action="store_true")
    parser.add_argument("--render", action="store_true")
    args = parser.parse_args()
    return evaluate(args)


if __name__ == "__main__":
    sys.exit(main())
