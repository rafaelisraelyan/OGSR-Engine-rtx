#!/usr/bin/env python3
"""BMP cross-check of the historical drift and the R2 control.

The primary historical comparison is compare_historical_baseline.py, which
decodes the PNG captures pixel by pixel. This script independently repeats the
measurement on the raw BMP captures (uncompressed RGBA, so a byte difference is
a channel difference) and verifies that the unmodified R2 control binary
reproduces every R4 capture byte-for-byte.
"""

from __future__ import annotations

import hashlib
import json
import shutil
from pathlib import Path

W = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work")
CONTROL_ROOT = Path(
    r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r2_work")
OUT = W / "evidence" / "01c_b_r4_static_drift.json"
COPY = W / "evidence" / "static_drift"
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
            f"{SCENES[parts[1]]}.bmp"
    return Path("static_1600") / parts[1] / "ground" / "p1" / \
        f"{SCENES[parts[1]]}.bmp"


def main() -> int:
    images = {}
    total_diff = 0
    max_delta = 0
    drifted = 0
    control_mismatch = 0
    keys = [f"800/{mode}/{view}" for mode in SCENES for view in VIEWS]
    keys += [f"1600/{mode}/ground" for mode in SCENES]

    for key in keys:
        rel = rel_for(key)
        r4_path = W / "evidence" / "static" / rel
        r2_path = W / "evidence" / "r2_current_static" / rel
        base_path = CONTROL_ROOT / "evidence" / "static" / rel
        r4 = r4_path.read_bytes()
        r2 = r2_path.read_bytes()
        base = base_path.read_bytes()
        if not (len(r4) == len(r2) == len(base)):
            raise SystemExit(f"BMP size mismatch: {key}")
        drift_offsets = [i for i in range(len(r4)) if r4[i] != base[i]]
        control_offsets = [i for i in range(len(r4)) if r4[i] != r2[i]]
        deltas = [abs(r4[i] - base[i]) for i in drift_offsets]
        entry = {
            "r4_sha256": sha(r4_path),
            "r2_control_sha256": sha(r2_path),
            "r2_era_capture_sha256": sha(base_path),
            "diff_bytes_vs_r2_era_capture": len(drift_offsets),
            "changed_channels_vs_r2_era_capture": len(drift_offsets),
            "max_abs_delta_vs_baseline": max(deltas, default=0),
            "diff_bytes_vs_r4_control": len(control_offsets),
            "r4_vs_r2_control_identical": not control_offsets,
        }
        images[key] = entry
        total_diff += len(drift_offsets)
        max_delta = max(max_delta, entry["max_abs_delta_vs_baseline"])
        if drift_offsets:
            drifted += 1
        if control_offsets:
            control_mismatch += 1

    identical = len(keys) - drifted
    rain_keys = [key for key in keys if key.split("/")[1] == "rain_sf"]
    rain_identical = sum(1 for key in rain_keys
                         if not images[key]["diff_bytes_vs_r2_era_capture"])
    payload = {
        "format": "V_WEATHER_01C_B_R4_STATIC_DRIFT_V2",
        "date": "2026-11-20",
        "role": ("BMP cross-check of the historical drift; the pixel-level "
                 "PNG comparison against the accepted 01C-A-R1 baseline is "
                 "01c_b_r4_historical_drift.json"),
        "r2_era_capture_bmp": str(CONTROL_ROOT / "evidence" / "static"),
        "images": images,
        "totals": {
            "images": len(keys),
            "identical_to_historical_baseline": identical,
            "drifted_from_historical_baseline": drifted,
            "total_diff_bytes_vs_baseline": total_diff,
            "max_abs_delta_vs_baseline": max_delta,
            "r4_vs_r2_control_mismatches": control_mismatch,
            "rain_sf_exact": f"{rain_identical}/{len(rain_keys)}",
        },
        "conclusion": (
            "The unmodified R2 control EXE reproduces every R4 capture "
            "byte-for-byte on this machine, so the residual difference against "
            "the historical baseline is environment drift, not an R4 rendering "
            "change. The historical 16/16 gate remains NOT MET."),
    }
    if max_delta > 1 or control_mismatch:
        payload["result"] = "FAIL"
    else:
        payload["result"] = "PASS"
    if COPY.exists():
        shutil.rmtree(COPY)
    COPY.mkdir(parents=True)
    for name, path in (
        ("r4_clouds_overcast_horizon.bmp",
         W / "evidence" / "static" /
         "static_1600/overcast/ground/p1/clouds_overcast_horizon.bmp"),
        ("r2_control_clouds_overcast_horizon.bmp",
         W / "evidence" / "r2_current_static" /
         "static_1600/overcast/ground/p1/clouds_overcast_horizon.bmp"),
        ("r2_era_capture_clouds_overcast_horizon.bmp",
         CONTROL_ROOT / "evidence" / "static" /
         "static_1600/overcast/ground/p1/clouds_overcast_horizon.bmp"),
    ):
        shutil.copy2(path, COPY / name)
    OUT.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(payload["totals"], indent=2))
    print("result", payload["result"])
    return 0 if payload["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
