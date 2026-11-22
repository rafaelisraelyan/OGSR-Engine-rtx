#!/usr/bin/env python3
"""Shared diagnostics for the V2.3h-A03b-R02 CPU research prototypes.

No third-party dependencies. The module deliberately keeps the R01 evaluator
untouched; it only fixes statistics, world-aspect rendering and evidence
metadata around it.
"""

import array
import hashlib
import json
import math
import struct
import zlib
from pathlib import Path

MASK32 = 0xFFFFFFFF
PERCENTILE_METHOD = "linear_interpolation_between_closest_ranks_0_based_ascending"


def percentile(values, q):
    """Percentile of an unsorted sequence, q in [0, 100], ascending order.

    Uses the common linear interpolation definition with zero-based ranks:
    pos = (n - 1) * q / 100. This is the definition used by NumPy's default
    ``linear`` method and is unambiguous for the small component samples in
    these checkpoints.
    """
    if not values:
        return 0.0
    if q <= 0.0:
        return float(min(values))
    if q >= 100.0:
        return float(max(values))
    ordered = sorted(float(v) for v in values)
    pos = (len(ordered) - 1) * (q / 100.0)
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return ordered[lo]
    frac = pos - lo
    return ordered[lo] + (ordered[hi] - ordered[lo]) * frac


def world_aspect_size(x_extent, y_extent, target_width):
    """Pixel size preserving the world X:Y aspect ratio.

    One world metre maps to the same number of pixels on both axes. The
    returned height is at least one pixel; callers should use nearest-neighbour
    sampling when the source grid resolution differs from the output size.
    """
    if x_extent <= 0.0 or y_extent <= 0.0:
        raise ValueError("extents must be positive")
    width = max(1, int(round(target_width)))
    height = max(1, int(round(width * (float(y_extent) / float(x_extent)))))
    return width, height


def resample_nearest(src, src_w, src_h, dst_w, dst_h):
    """Nearest-neighbour RGB resample of a row-major 3-byte image."""
    if src_w == dst_w and src_h == dst_h:
        return bytes(src)
    out = bytearray(dst_w * dst_h * 3)
    for y in range(dst_h):
        sy = min(src_h - 1, int(y * src_h / dst_h))
        srow = sy * src_w * 3
        drow = y * dst_w * 3
        for x in range(dst_w):
            sx = min(src_w - 1, int(x * src_w / dst_w))
            si = srow + sx * 3
            di = drow + x * 3
            out[di:di + 3] = src[si:si + 3]
    return bytes(out)


def png_write(path, width, height, rgb):
    """Write an 8-bit RGB PNG. ``rgb`` must be width*height*3 bytes."""
    if len(rgb) != width * height * 3:
        raise ValueError("pixel buffer does not match dimensions")
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw.extend(rgb[y * stride:(y + 1) * stride])

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & MASK32))

    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n")
        handle.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        handle.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        handle.write(chunk(b"IEND", b""))


def rgb_rows_to_buffer(rows):
    """Concatenate per-row bytearrays into one RGB buffer."""
    return b"".join(bytes(row) for row in rows)


def color_map(v, stops=None):
    """Dark blue -> cyan -> yellow -> white diagnostic colour map."""
    if stops is None:
        stops = ((0.0, (4, 6, 18)), (0.25, (0, 90, 170)),
                 (0.5, (0, 200, 170)), (0.75, (255, 190, 0)),
                 (1.0, (255, 245, 235)))
    v = max(0.0, min(1.0, float(v)))
    for i in range(len(stops) - 1):
        a, ca = stops[i]
        b, cb = stops[i + 1]
        if a <= v <= b:
            t = (v - a) / (b - a) if b > a else 0.0
            return tuple(int(round(ca[k] + (cb[k] - ca[k]) * t)) for k in range(3))
    return stops[-1][1]


def _components_2d(mask, nx, nz):
    seen = bytearray(nx * nz)
    areas = []
    for start in range(nx * nz):
        if not mask[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        area = 0
        while stack:
            i = stack.pop()
            area += 1
            x = i % nx
            z = i // nx
            if x > 0:
                j = i - 1
                if mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
            if x + 1 < nx:
                j = i + 1
                if mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
            if z > 0:
                j = i - nx
                if mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
            if z + 1 < nz:
                j = i + nx
                if mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
        areas.append(area)
    return sorted(areas)


def component_stats_2d(mask, nx, nz, cell_area_m2):
    areas = _components_2d(mask, nx, nz)
    total = sum(areas)
    total_cells = nx * nz
    return {
        "dimension": 2,
        "connectivity": 4,
        "grid": [nx, nz],
        "cell_area_m2": cell_area_m2,
        "occupied_cells": total,
        "occupied_fraction": total / total_cells if total_cells else 0.0,
        "count": len(areas),
        "largest_cells": areas[-1] if areas else 0,
        "largest_fraction": (areas[-1] / total) if total else 0.0,
        "p50_area_cells": percentile(areas, 50.0),
        "p90_area_cells": percentile(areas, 90.0),
        "p95_area_cells": percentile(areas, 95.0),
        "p50_area_m2": percentile(areas, 50.0) * cell_area_m2,
        "p90_area_m2": percentile(areas, 90.0) * cell_area_m2,
        "p95_area_m2": percentile(areas, 95.0) * cell_area_m2,
        "percentile_method": PERCENTILE_METHOD,
    }


def _components_3d(mask, nx, ny, nz):
    """6-connected 3D components, index order x fastest, then y, then z."""
    seen = bytearray(nx * ny * nz)
    volumes = []
    plane = nx * ny
    for start in range(nx * ny * nz):
        if not mask[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        volume = 0
        while stack:
            i = stack.pop()
            volume += 1
            x = i % nx
            y = (i // nx) % ny
            z = i // plane
            if x > 0:
                j = i - 1
                if mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
            if x + 1 < nx:
                j = i + 1
                if mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
            if y > 0:
                j = i - nx
                if mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
            if y + 1 < ny:
                j = i + nx
                if mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
            if z > 0:
                j = i - plane
                if mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
            if z + 1 < nz:
                j = i + plane
                if mask[j] and not seen[j]:
                    seen[j] = 1
                    stack.append(j)
        volumes.append(volume)
    return sorted(volumes)


def component_stats_3d(mask, nx, ny, nz, voxel_volume_m3):
    volumes = _components_3d(mask, nx, ny, nz)
    total = sum(volumes)
    total_voxels = nx * ny * nz
    return {
        "dimension": 3,
        "connectivity": 6,
        "grid": [nx, ny, nz],
        "index_order": "x fastest, then y, then z",
        "voxel_volume_m3": voxel_volume_m3,
        "occupied_voxels": total,
        "occupied_fraction": total / total_voxels if total_voxels else 0.0,
        "count": len(volumes),
        "largest_voxels": volumes[-1] if volumes else 0,
        "largest_fraction": (volumes[-1] / total) if total else 0.0,
        "p50_volume_cells": percentile(volumes, 50.0),
        "p90_volume_cells": percentile(volumes, 90.0),
        "p95_volume_cells": percentile(volumes, 95.0),
        "p50_volume_m3": percentile(volumes, 50.0) * voxel_volume_m3,
        "p90_volume_m3": percentile(volumes, 90.0) * voxel_volume_m3,
        "p95_volume_m3": percentile(volumes, 95.0) * voxel_volume_m3,
        "percentile_method": PERCENTILE_METHOD,
    }


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def write_float32_field(path, values, metadata):
    """Write Float32 little-endian values plus JSON metadata and checksum."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as handle:
        array.array("f", values).tofile(handle)
    digest = sha256_file(path)
    meta = dict(metadata)
    meta["data_file"] = path.name
    meta["component_type"] = "float32_le"
    meta["element_count"] = len(values)
    meta["sha256"] = digest
    validate_field_metadata(meta)
    json_path = path.with_suffix(".json")
    with open(json_path, "w", encoding="utf-8") as handle:
        json.dump(meta, handle, indent=2)
        handle.write("\n")
    return digest


def read_float32_field(path, metadata):
    """Read and verify a Float32 field produced by write_float32_field."""
    path = Path(path)
    values = array.array("f")
    values.frombytes(path.read_bytes())
    if int(metadata["element_count"]) != len(values):
        raise ValueError("element_count mismatch")
    digest = sha256_file(path)
    if digest.lower() != str(metadata["sha256"]).lower():
        raise ValueError("sha256 mismatch")
    return values


REQUIRED_FIELD_METADATA = (
    "format", "field", "seed", "grid", "world_origin", "cell_size_m",
    "axes", "index_order", "component_type", "element_count", "sha256",
    "algorithm", "parameters", "source_sha256", "provenance",
)


def validate_field_metadata(meta):
    missing = [key for key in REQUIRED_FIELD_METADATA if key not in meta]
    if missing:
        raise ValueError("missing metadata keys: " + ", ".join(missing))
    if meta["component_type"] != "float32_le":
        raise ValueError("unsupported component type")
    return True
