#!/usr/bin/env python3
"""Compare R3 static PNGs byte-for-byte with the accepted P1 baseline."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

W = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work")
CUR = W / "evidence" / "static"
REF = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\package_01c_a_r1\static")
OUT_JSON = W / "evidence" / "01c_b_r4_static_identity.json"
OUT_TXT = W / "evidence" / "01c_b_r4_static_identity.txt"
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
        "format": "V_WEATHER_01C_B_R4_STATIC_IDENTITY_V1",
        "date": "2026-11-20",
        "reference": str(REF),
        "items": {},
        "result": "PASS",
    }
    for mode, scene in SCENES.items():
        for view in VIEWS:
            key = f"800/{mode}/{view}"
            cur = (CUR / "static_800" / mode / view / "p1" /
                   f"{scene}.png")
            ref = (REF / "static_800" / mode / view / "p1" /
                   f"{scene}.png")
            entry = {"current_sha256": sha(cur),
                     "reference_sha256": sha(ref)}
            entry["identical"] = (
                entry["current_sha256"] is not None and
                entry["current_sha256"] == entry["reference_sha256"])
            if not entry["identical"]:
                result["result"] = "FAIL"
            result["items"][key] = entry
        key = f"1600/{mode}/ground"
        cur = (CUR / "static_1600" / mode / "ground" / "p1" /
               f"{scene}.png")
        ref = (REF / "static_1600" / mode / "ground" / "p1" /
               f"{scene}.png")
        entry = {"current_sha256": sha(cur),
                 "reference_sha256": sha(ref)}
        entry["identical"] = (
            entry["current_sha256"] is not None and
            entry["current_sha256"] == entry["reference_sha256"])
        if not entry["identical"]:
            result["result"] = "FAIL"
        result["items"][key] = entry
    OUT_JSON.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    lines = ["V-WEATHER-01C-B-R4 static identity", "=" * 46, ""]
    lines.append(f"Overall: {result['result']}")
    for key, entry in result["items"].items():
        lines.append(
            f"{key}: identical={entry['identical']} "
            f"sha={entry['current_sha256']}")
    OUT_TXT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("result", result["result"], "items", len(result["items"]))
    return 0 if result["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())

