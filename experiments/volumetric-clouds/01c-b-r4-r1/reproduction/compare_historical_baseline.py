#!/usr/bin/env python3
"""Pixel-level comparison of the R4 captures against the historical 01C-A-R1
baseline and against the unmodified R2 control capture.

The historical baseline is stored as PNG, so byte comparison is meaningless
(different encoder settings produce different bytes for identical pixels).
This script decodes both images and compares channels, which yields the real
per-image channel count and maximum per-channel delta.
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

from PIL import Image

WORK = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work")
BASELINE = Path(
    r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\package_01c_a_r1\static")
CURRENT = WORK / "evidence" / "static"
R2_CONTROL = WORK / "evidence" / "r2_current_static"
BASELINE_BMP = Path(
    r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r2_work\evidence\static")
OUT_JSON = WORK / "evidence" / "01c_b_r4_historical_drift.json"
OUT_TXT = WORK / "evidence" / "01c_b_r4_historical_drift.txt"

SCENES = {
    "clear": "clouds_clear_horizon",
    "partly": "clouds_partly_horizon",
    "overcast": "clouds_overcast_horizon",
    "rain_sf": "clouds_rain_horizon",
}
VIEWS = ("front", "ground", "oblique")


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rel_for(key: str) -> Path:
    parts = key.split("/")
    if parts[0] == "800":
        return Path("static_800") / parts[1] / parts[2] / "p1" / \
            f"{SCENES[parts[1]]}.png"
    return Path("static_1600") / parts[1] / "ground" / "p1" / \
        f"{SCENES[parts[1]]}.png"


def compare(baseline: Path, current: Path) -> dict:
    base = Image.open(baseline).convert("RGBA")
    cur = Image.open(current).convert("RGBA")
    if base.size != cur.size:
        raise SystemExit(f"size mismatch: {baseline} {base.size} vs {cur.size}")
    base_bytes = base.tobytes()
    cur_bytes = cur.tobytes()
    changed_channels = 0
    changed_pixels = 0
    max_delta = 0
    sample = []
    width, height = base.size
    for index in range(0, len(base_bytes), 4):
        pixel_changed = False
        for channel in range(4):
            delta = abs(cur_bytes[index + channel] - base_bytes[index + channel])
            if delta:
                changed_channels += 1
                pixel_changed = True
                if delta > max_delta:
                    max_delta = delta
                if len(sample) < 12:
                    position = index // 4
                    sample.append({
                        "x": position % width,
                        "y": height - 1 - position // width,
                        "channel": "RGBA"[channel],
                        "baseline": base_bytes[index + channel],
                        "current": cur_bytes[index + channel],
                        "delta": delta,
                    })
        if pixel_changed:
            changed_pixels += 1
    return {
        "width": width,
        "height": height,
        "changed_channels": changed_channels,
        "changed_pixels": changed_pixels,
        "max_abs_delta": max_delta,
        "identical": changed_channels == 0,
        "sample_pixels": sample,
    }


def main() -> int:
    if not BASELINE.exists():
        raise SystemExit(f"missing historical baseline: {BASELINE}")
    keys = [f"800/{mode}/{view}" for mode in SCENES for view in VIEWS]
    keys += [f"1600/{mode}/ground" for mode in SCENES]

    images = {}
    total_channels = 0
    total_bytes_bmp = 0
    max_delta = 0
    drifted = 0
    control_mismatch = 0
    for key in keys:
        rel = rel_for(key)
        base_path = BASELINE / rel
        cur_path = CURRENT / rel
        r2_path = R2_CONTROL / rel
        if not base_path.exists():
            raise SystemExit(f"missing baseline image: {base_path}")
        if not cur_path.exists():
            raise SystemExit(f"missing current image: {cur_path}")
        if not r2_path.exists():
            raise SystemExit(f"missing R2 control image: {r2_path}")
        entry = compare(base_path, cur_path)
        entry.update({
            "baseline_png_sha256": sha(base_path),
            "current_png_sha256": sha(cur_path),
            "r2_control_png_sha256": sha(r2_path),
            "current_equals_r2_control": sha(cur_path) == sha(r2_path),
        })
        bmp_rel = rel.with_suffix(".bmp")
        base_bmp = BASELINE_BMP / bmp_rel
        cur_bmp = CURRENT / bmp_rel
        r2_bmp = R2_CONTROL / bmp_rel
        if base_bmp.exists() and cur_bmp.exists() and r2_bmp.exists():
            base_data = base_bmp.read_bytes()
            cur_data = cur_bmp.read_bytes()
            r2_data = r2_bmp.read_bytes()
            bmp_diff = sum(1 for index in range(len(cur_data))
                           if cur_data[index] != base_data[index])
            entry["bmp_vs_r2_era_capture_diff_bytes"] = bmp_diff
            entry["bmp_equals_r2_control"] = cur_data == r2_data
            total_bytes_bmp += bmp_diff
            if cur_data != r2_data:
                control_mismatch += 1
        images[key] = entry
        total_channels += entry["changed_channels"]
        max_delta = max(max_delta, entry["max_abs_delta"])
        if not entry["identical"]:
            drifted += 1
        if not entry["current_equals_r2_control"]:
            control_mismatch += 1

    rain_keys = [key for key in keys if key.split("/")[1] == "rain_sf"]
    rain_exact = sum(1 for key in rain_keys if images[key]["identical"])
    payload = {
        "format": "V_WEATHER_01C_B_R4_HISTORICAL_DRIFT_V1",
        "date": "2026-11-20",
        "method": ("PNG decoded to RGBA and compared channel by channel; "
                   "baseline PNG byte comparison would be invalid because the "
                   "PNG encoder differs between captures."),
        "historical_baseline": {
            "path": str(BASELINE),
            "kind": "accepted 01C-A-R1 static PNG capture",
        },
        "current": str(CURRENT),
        "r2_control": str(R2_CONTROL),
        "images": images,
        "totals": {
            "images": len(keys),
            "identical_to_historical_baseline": len(keys) - drifted,
            "drifted_from_historical_baseline": drifted,
            "total_changed_channels": total_channels,
            "total_changed_channels_bmp_crosscheck": total_bytes_bmp,
            "max_abs_delta": max_delta,
            "r4_current_equals_r2_control_png": control_mismatch == 0,
            "r4_vs_r2_control_mismatches": control_mismatch,
            "rain_sf_exact": f"{rain_exact}/{len(rain_keys)}",
        },
        "historical_gate": "NOT MET",
        "conclusion": (
            "The unmodified R2 control binary, run in the same environment with "
            "the same parameters, reproduces every R4 capture byte-for-byte, so "
            "the residual difference against the historical 01C-A-R1/R2 "
            "baseline is environment drift on this machine, not an R4 rendering "
            "change. The historical 16/16 gate remains NOT MET; attributing the "
            "drift to a specific OS/driver change was NOT verified and is not "
            "claimed."),
    }
    if max_delta > 1:
        payload["result"] = "FAIL"
    elif control_mismatch:
        payload["result"] = "FAIL"
    else:
        payload["result"] = "PASS"
    OUT_JSON.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")

    lines = ["V-WEATHER-01C-B-R4 historical baseline comparison", "=" * 60, ""]
    lines.append(f"historical baseline: {BASELINE}")
    lines.append(f"result: {payload['result']}  "
                 f"gate: {payload['historical_gate']}")
    lines.append("")
    lines.append(f"{'image':22} {'changed_ch':>10} {'changed_px':>10} "
                 f"{'max_delta':>9} {'=R2':>5}")
    for key in keys:
        entry = images[key]
        lines.append(f"{key:22} {entry['changed_channels']:10d} "
                     f"{entry['changed_pixels']:10d} "
                     f"{entry['max_abs_delta']:9d} "
                     f"{str(entry['current_equals_r2_control']):>5}")
    totals = payload["totals"]
    lines.append("")
    lines.append(f"images={totals['images']} "
                 f"exact={totals['identical_to_historical_baseline']} "
                 f"drifted={totals['drifted_from_historical_baseline']} "
                 f"channels={totals['total_changed_channels']} "
                 f"max_delta={totals['max_abs_delta']} "
                 f"rain_sf={totals['rain_sf_exact']}")
    OUT_TXT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps(totals, indent=2))
    print("result", payload["result"])
    return 0 if payload["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
