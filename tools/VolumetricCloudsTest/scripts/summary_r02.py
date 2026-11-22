#!/usr/bin/env python3
"""R02 evidence summary: seed tables, determinism, alias and R01 corrections."""

import array
import hashlib
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r02_diagnostics import percentile

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / "artifacts" / "v23h_a03b_r02" / "final"
SEEDS = (1337, 4242, 99, 2027, 9001, 9529)


def load_json(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def sha(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def read_f32(path):
    values = array.array("f")
    values.frombytes(path.read_bytes())
    return values


def corr(a, b):
    n = min(len(a), len(b))
    ma = sum(a[:n]) / n
    mb = sum(b[:n]) / n
    num = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
    da = math.sqrt(sum((a[i] - ma) ** 2 for i in range(n)))
    db = math.sqrt(sum((b[i] - mb) ** 2 for i in range(n)))
    return num / (da * db) if da > 0.0 and db > 0.0 else 0.0


def family_table(family, scale):
    rows = []
    for seed in SEEDS:
        path = ART / family / f"{scale}_s{seed}" / f"stats_s{seed}.json"
        stats = load_json(path)
        v = stats["vertical"]
        c3 = stats["component_3d"]
        c2 = stats["component_2d_xzmax"]
        rows.append({
            "seed": seed,
            "occupied_columns": stats["occupied_columns"],
            "occupied_voxels": stats["occupied_voxels"],
            "components_2d": c2["count"],
            "largest_2d_fraction": c2["largest_fraction"],
            "components_3d": c3["count"],
            "largest_3d_fraction": c3["largest_fraction"],
            "thickness_p50_m": v.get("thickness_m", {}).get("p50", 0.0),
            "thickness_p90_m": v.get("thickness_m", {}).get("p90", 0.0),
            "top_std_m": v.get("top_m", {}).get("std", 0.0),
            "bottom_std_m": v.get("bottom_m", {}).get("std", 0.0),
            "intervals_fraction_gt1": v.get("intervals_per_column", {}).get("fraction_gt1", 0.0),
            "unique_profile_fraction": v.get("profile_signatures", {}).get("unique_fraction", 0.0),
            "neighbour_corr_top_x": v.get("neighbour_corr_top_x", 0.0),
            "neighbour_corr_top_z": v.get("neighbour_corr_top_z", 0.0),
            "autocorr_7km": stats.get("autocorrelation_xzmax", {}).get("7000", {}).get("corr", 0.0),
        })
    return rows


def main():
    summary = {
        "hcs1": {
            "local": family_table("hcs1", "local"),
            "regional": family_table("hcs1", "regional"),
        },
        "hcs2": {
            "local": family_table("hcs2", "local"),
            "regional": family_table("hcs2", "regional"),
        },
    }

    # Determinism: fields must be byte-identical across repeated runs.
    determinism = {}
    for name in ("xzmax_s1337.bin", "system_s1337.bin", "mass_s1337.bin",
                 "vertical_s1337.bin", "occupancy_s1337.bin", "density_s1337.bin"):
        a = ART / "hcs1" / "determinism_repeat_a" / name
        b = ART / "hcs1" / "determinism_repeat_b" / name
        determinism[name] = {
            "a": sha(a),
            "b": sha(b),
            "identical": sha(a) == sha(b),
        }
    summary["determinism"] = determinism

    # Large world coordinate check: finite fields and independent samples.
    origin = load_json(ART / "hcs1" / "world_origin" / "stats_s1337.json")
    offset = load_json(ART / "hcs1" / "world_offset" / "stats_s1337.json")
    summary["large_world_coordinates"] = {
        "origin_center": [0.0, 0.0],
        "offset_center": [1000000.0, -500000.0],
        "origin_occupied_columns": origin["occupied_columns"],
        "offset_occupied_columns": offset["occupied_columns"],
        "origin_occupied_voxels": origin["occupied_voxels"],
        "offset_occupied_voxels": offset["occupied_voxels"],
        "offset_finite": all(math.isfinite(v) for v in (
            offset["occupied_columns"], offset["occupied_voxels"],
            offset["vertical"]["thickness_m"]["p50"])),
    }

    # Alias check for the two site-grid seeds where both fields are non-empty.
    a = read_f32(ART / "hcs2" / "local_s1337" / "xzmax_s1337.bin")
    b = read_f32(ART / "hcs2" / "local_s9529" / "xzmax_s9529.bin")
    summary["hcs2_alias_1337_9529_xzmax_corr"] = corr(a, b)

    # R01 diagnostic correction: show old and corrected p90.
    r01 = ROOT / "artifacts" / "v23h_a03b_r01"
    old = load_json(r01 / "analysis_s1337.json")
    new = load_json(r01 / "analysis_s1337_corrected.json")
    old60 = load_json(r01 / "analysis_60km.json")
    new60 = load_json(r01 / "analysis_60km_corrected.json")
    summary["r01_percentile_correction"] = {
        "local_old_p90_cells": old["components"]["p90_area_cells"],
        "local_new_p90_cells": new["components"]["p90_area_cells"],
        "regional_old_p90_cells": old60["components"]["p90_area_cells"],
        "regional_new_p90_cells": new60["components"]["p90_area_cells"],
        "note": "old analyzer indexed a descending area list at 0.9*n",
    }

    with open(ART / "summary_r02.json", "w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
        handle.write("\n")

    lines = ["# R02 seed summary", ""]
    for family in ("hcs1", "hcs2"):
        for scale in ("local", "regional"):
            lines.append(f"## {family} {scale}")
            lines.append("| seed | cols | vox | 3D comp | largest 3D | thick p50 | thick p90 | top std | unique profiles |")
            lines.append("|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
            for row in summary[family][scale]:
                lines.append("| {seed} | {occupied_columns:.1%} | {occupied_voxels:.1%} | {components_3d} | {largest_3d_fraction:.1%} | {thickness_p50_m:.0f} | {thickness_p90_m:.0f} | {top_std_m:.0f} | {unique_profile_fraction:.1%} |".format(**row))
            lines.append("")
    lines.append("## Determinism")
    for name, item in determinism.items():
        lines.append(f"- `{name}` identical: {item['identical']}")
    lines.append("")
    lines.append(f"HCS2 alias corr(1337,9529) = {summary['hcs2_alias_1337_9529_xzmax_corr']:.6f}")
    lines.append(f"Large-coordinate offset finite: {summary['large_world_coordinates']['offset_finite']} "
                 f"({summary['large_world_coordinates']['origin_occupied_columns']:.1%} -> "
                 f"{summary['large_world_coordinates']['offset_occupied_columns']:.1%} columns)")
    lines.append("")
    lines.append("## R01 p90 correction")
    for key, value in summary["r01_percentile_correction"].items():
        if key != "note":
            lines.append(f"- {key}: {value}")
    (ART / "SUMMARY.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
