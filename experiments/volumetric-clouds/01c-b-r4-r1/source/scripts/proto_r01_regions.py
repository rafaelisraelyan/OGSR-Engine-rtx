#!/usr/bin/env python3
"""R01 CPU feasibility prototype: region-based 3D occupancy.

World-space jittered sites on an 8 km XZ lattice. Each site is an anisotropic
3D superellipsoid/rounded box with independent center height, horizontal
radii, top/bottom vertical radii, yaw and box/ellipsoid blend. A sample
evaluates the 3x3 XZ neighborhood and takes the max field. No periodic
NoiseVolume, no camera dependence, no per-sample unbounded loop.
"""

import argparse
import array
import json
import math
import struct
import sys
import zlib
from pathlib import Path

CELL = 7000.0
LAYER_BOTTOM = 1200.0
LAYER_TOP = 4000.0
MASK = 0xFFFFFFFF


def h32(a, b, seed, salt=0):
    v = (a * 374761393 + b * 668265263 + seed * 2246822519 + salt * 3266489917) & MASK
    v ^= v >> 15
    v = (v * 0x2C1B3C6D) & MASK
    v ^= v >> 12
    v = (v * 0x297A2D39) & MASK
    v ^= v >> 15
    return v


class Rng:
    def __init__(self, v):
        self.v = v & MASK

    def next(self):
        self.v ^= (self.v << 13) & MASK
        self.v ^= self.v >> 17
        self.v ^= (self.v << 5) & MASK
        self.v &= MASK
        return self.v / 4294967296.0


class Site:
    __slots__ = ("cx", "cy", "cz", "rx", "rz", "ryt", "ryb", "yaw", "box", "p", "ph1", "ph2")

    def __init__(self, ix, iz, seed, slot):
        r = Rng(h32(ix, iz, seed, slot + 1))
        # Keep centers in the central 60% of the cell so 3x3 is sufficient.
        self.cx = (ix + 0.15 + 0.70 * r.next()) * CELL
        self.cz = (iz + 0.15 + 0.70 * r.next()) * CELL
        self.cy = LAYER_BOTTOM + (LAYER_TOP - LAYER_BOTTOM) * (0.18 + 0.64 * r.next())
        self.rx = CELL * (0.28 + 0.27 * r.next())
        self.rz = CELL * (0.28 + 0.27 * r.next())
        thickness = LAYER_TOP - LAYER_BOTTOM
        self.ryt = thickness * (0.12 + 0.30 * r.next())
        self.ryb = thickness * (0.12 + 0.28 * r.next())
        self.yaw = r.next() * 2.0 * math.pi
        self.box = 0.10 + 0.70 * r.next()
        self.p = 1.5 + 2.0 * r.next()
        self.ph1 = 1.0 if r.next() > 0.5 else -1.0
        self.ph2 = 1.0 if r.next() > 0.5 else -1.0


_SITE_CACHE = {}


def site(ix, iz, seed, slot):
    key = (ix, iz, seed, slot)
    s = _SITE_CACHE.get(key)
    if s is None:
        s = Site(ix, iz, seed, slot)
        _SITE_CACHE[key] = s
    return s


def field(x, y, z, seed):
    ix = math.floor(x / CELL)
    iz = math.floor(z / CELL)
    best = -1e9
    for cz in (iz - 1, iz, iz + 1):
        for cx in (ix - 1, ix, ix + 1):
            for slot in (0, 1):
                s = site(cx, cz, seed, slot)
                dx = x - s.cx
                dz = z - s.cz
                c = math.cos(s.yaw)
                sn = math.sin(s.yaw)
                rx = (dx * c - dz * sn) / s.rx
                rz = (dx * sn + dz * c) / s.rz
                dy = y - s.cy
                ry = s.ryt if dy > 0.0 else s.ryb
                zy = dy / ry
                # Ellipsoid and rounded-box blend; no pow().
                e = math.sqrt(rx * rx + rz * rz + zy * zy)
                b = max(abs(rx), abs(rz), abs(zy))
                d = e + (b - e) * s.box
                # Smooth non-ellipsoidal warp: asymmetric bulges and a
                # vertical tilt. No high-frequency ripple or pow().
                d += s.ph1 * 0.10 * (rx * rz)
                d += s.ph2 * 0.06 * (zy * rz)
                f = 1.0 - d
                if f > best:
                    best = f
    return best


def smoothstep(a, b, x):
    t = max(0.0, min(1.0, (x - a) / (b - a)))
    return t * t * (3.0 - 2.0 * t)


def occupancy(x, y, z, seed, cut, width=0.08):
    return smoothstep(cut - width, cut + width, field(x, y, z, seed))


def png(path, width, height, rows):
    raw = bytearray()
    for row in rows:
        raw.append(0)
        raw.extend(row)
    def chunk(tag, payload):
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & MASK)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def color(v):
    v = max(0.0, min(1.0, v))
    # dark blue -> cyan -> yellow -> white
    stops = ((0.0, (4, 6, 18)), (0.25, (0, 90, 170)), (0.5, (0, 200, 170)),
             (0.75, (255, 190, 0)), (1.0, (255, 245, 235)))
    for i in range(len(stops) - 1):
        a, ca = stops[i]; b, cb = stops[i + 1]
        if a <= v <= b:
            t = (v - a) / (b - a)
            return tuple(round(ca[k] + (cb[k] - ca[k]) * t) for k in range(3))
    return stops[-1][1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=1337)
    ap.add_argument("--cut", type=float, default=0.55)
    ap.add_argument("--size", type=float, default=16000.0)
    ap.add_argument("--res", type=int, default=128)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    x0 = -args.size / 2
    z0 = -args.size / 2
    step = args.size / args.res
    # XZ slice at y=2500
    rows = []
    for iz in range(args.res):
        row = bytearray()
        for ix in range(args.res):
            x = x0 + (ix + 0.5) * step
            z = z0 + (iz + 0.5) * step
            row.extend(color(occupancy(x, 2500.0, z, args.seed, args.cut)))
        rows.append(row)
    png(args.out / (f"xz_s{args.seed}_c{args.cut:.2f}.png"), args.res, args.res, rows)
    # XZ column-max (true occupied-column map)
    rows = []
    ycount = 20
    column_values = array.array("f")
    for iz in range(args.res):
        row = bytearray()
        for ix in range(args.res):
            x = x0 + (ix + 0.5) * step
            z = z0 + (iz + 0.5) * step
            top = 0.0
            for iy in range(ycount):
                y = LAYER_BOTTOM + (iy + 0.5) * (LAYER_TOP - LAYER_BOTTOM) / ycount
                o = occupancy(x, y, z, args.seed, args.cut)
                if o > top:
                    top = o
            column_values.append(top)
            row.extend(color(top))
        rows.append(row)
    png(args.out / (f"xzmax_s{args.seed}_c{args.cut:.2f}.png"), args.res, args.res, rows)
    stem = args.out / (f"xzmax_s{args.seed}_c{args.cut:.2f}")
    with open(str(stem) + ".bin", "wb") as handle:
        column_values.tofile(handle)
    with open(str(stem) + ".json", "w", encoding="utf-8") as handle:
        json.dump({
            "format": "OGSR_V2_3h_A03b_R01_cpu_xzmax_float32",
            "seed": args.seed,
            "cut": args.cut,
            "grid": [args.res, args.res],
            "world_origin": [x0, 0.0, z0],
            "cell_size_xz_m": step,
            "index_order": "x fastest, then z",
            "component_type": "float32_le",
            "channel": "column_max_occupancy",
        }, handle, indent=2)
    # XY slice at z=0
    rows = []
    for iy in range(args.res):
        row = bytearray()
        y = LAYER_BOTTOM + (iy + 0.5) * (LAYER_TOP - LAYER_BOTTOM) / args.res
        for ix in range(args.res):
            x = x0 + (ix + 0.5) * step
            row.extend(color(occupancy(x, y, 0.0, args.seed, args.cut)))
        rows.append(row)
    png(args.out / (f"xy_s{args.seed}_c{args.cut:.2f}.png"), args.res, args.res, rows)
    # Stats and column occupancy on a 16 km volume
    n = 48
    ys = 24
    cols = 0
    vox = 0
    total = 0
    for iz in range(n):
        for ix in range(n):
            x = x0 + (ix + 0.5) * (args.size / n)
            z = z0 + (iz + 0.5) * (args.size / n)
            top = 0.0
            for iy in range(ys):
                y = LAYER_BOTTOM + (iy + 0.5) * (LAYER_TOP - LAYER_BOTTOM) / ys
                o = occupancy(x, y, z, args.seed, args.cut)
                top = max(top, o)
                if o > 0.2:
                    vox += 1
                total += 1
            if top > 0.2:
                cols += 1
    print("seed=%d cut=%.2f cols=%.3f vox=%.3f size=%.0f" %
          (args.seed, args.cut, cols / (n * n), vox / total, args.size))


if __name__ == "__main__":
    main()
