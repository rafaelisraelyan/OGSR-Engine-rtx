#!/usr/bin/env python3
"""Compare R4 against the unmodified R2 control binary on this machine."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

W = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work")
R4 = W / "evidence" / "static"
R2 = W / "evidence" / "r2_current_static"
OUT_JSON = W / "evidence" / "01c_b_r4_vs_r2_static.json"
OUT_TXT = W / "evidence" / "01c_b_r4_vs_r2_static.txt"
SCENES = {
    "clear": "clouds_clear_horizon",
    "partly": "clouds_partly_horizon",
    "overcast": "clouds_overcast_horizon",
    "rain_sf": "clouds_rain_horizon",
}
VIEWS = ("front", "ground", "oblique")


def sha(path: Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None


def main() -> int:
    result = {
        "format": "V_WEATHER_01C_B_R4_VS_R2_STATIC_V1",
        "date": "2026-11-20",
        "r4": str(R4),
        "r2": str(R2),
        "items": {},
        "result": "PASS",
    }
    for mode, scene in SCENES.items():
        for view in VIEWS:
            key = f"800/{mode}/{view}"
            rel = Path("static_800") / mode / view / "p1" / f"{scene}.png"
            entry = {"r4_sha256": sha(R4 / rel), "r2_sha256": sha(R2 / rel)}
            entry["identical"] = (
                entry["r4_sha256"] is not None and
                entry["r4_sha256"] == entry["r2_sha256"])
            if not entry["identical"]:
                result["result"] = "FAIL"
            result["items"][key] = entry
        key = f"1600/{mode}/ground"
        rel = Path("static_1600") / mode / "ground" / "p1" / f"{scene}.png"
        entry = {"r4_sha256": sha(R4 / rel), "r2_sha256": sha(R2 / rel)}
        entry["identical"] = (
            entry["r4_sha256"] is not None and
            entry["r4_sha256"] == entry["r2_sha256"])
        if not entry["identical"]:
            result["result"] = "FAIL"
        result["items"][key] = entry
    OUT_JSON.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    lines = ["V-WEATHER-01C-B-R4 vs unmodified R2 control", "=" * 52, ""]
    lines.append(f"Overall: {result['result']}")
    for key, entry in result["items"].items():
        lines.append(
            f"{key}: identical={entry['identical']} "
            f"r4={entry['r4_sha256']} r2={entry['r2_sha256']}")
    OUT_TXT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("result", result["result"], "items", len(result["items"]))
    return 0 if result["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

