#!/usr/bin/env python3
"""Summarize V-WEATHER-01C-B D3D12 Debug Layer / GPU validation runs."""

from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work")
E = ROOT / "evidence" / "gbv"
OUT_JSON = ROOT / "evidence" / "01c_b_r4_gbv_summary.json"
OUT_TXT = ROOT / "evidence" / "01c_b_r4_gbv_summary.txt"
CASES = ("runtime_800", "runtime_1600", "clouds_on", "clouds_off",
         "transition_debug")
PREFLIGHT_RE = re.compile(r"validation preflight:.*?->\s*(PASS|FAIL)")
TOTALS_RE = re.compile(
    r"debug message totals:\s*(\d+)\s*errors,\s*(\d+)\s*warnings")
ERROR_RE = re.compile(r"\[d3d12 ERROR\]")
WARNING_RE = re.compile(r"\[d3d12 WARNING\]")
LIVE_RE = re.compile(r"\[d3d12 WARNING\]\[live objects")
NON_DEVICE_RE = re.compile(r"non-device objects:\s*(\d+)")
DIVISOR_RE = re.compile(r"cloud output divisor changed:")
RESIZE_RE = re.compile(r"cloud output resized:")
SHUTDOWN_RE = re.compile(r"cloud ray marcher shutdown: generation=(\d+)")


def main() -> int:
    result = {
        "format": "V_WEATHER_01C_B_R4_GBV_V1",
        "date": "2026-11-20",
        "runs": {},
        "overall": "PASS",
        "totals": {},
    }
    errors = warnings = live = 0
    for case in CASES:
        log = E / case / "run.log"
        if not log.exists():
            result["runs"][case] = {"exists": False}
            result["overall"] = "FAIL"
            continue
        text = log.read_text(encoding="utf-8", errors="replace")
        preflight = PREFLIGHT_RE.search(text)
        totals = TOTALS_RE.search(text)
        shutdown = SHUTDOWN_RE.search(text)
        entry = {
            "exists": True,
            "preflight": preflight.group(1) if preflight else "MISSING",
            "debug_errors": int(totals.group(1)) if totals else None,
            "debug_warnings": int(totals.group(2)) if totals else None,
            "d3d12_error_lines": len(ERROR_RE.findall(text)),
            "d3d12_warning_lines": len(WARNING_RE.findall(text)),
            "live_object_warning_lines": len(LIVE_RE.findall(text)),
            "non_device_live_objects": (
                int(NON_DEVICE_RE.search(text).group(1))
                if NON_DEVICE_RE.search(text) else None),
            "divisor_changes": len(DIVISOR_RE.findall(text)),
            "resizes": len(RESIZE_RE.findall(text)),
            "shutdown_generation": int(shutdown.group(1)) if shutdown else None,
        }
        result["runs"][case] = entry
        errors += entry["d3d12_error_lines"]
        warnings += entry["d3d12_warning_lines"]
        live += entry["live_object_warning_lines"]
        if (entry["preflight"] != "PASS" or entry["d3d12_error_lines"] != 0
                or (entry["debug_errors"] or 0) != 0
                or (entry["non_device_live_objects"] or 0) != 0):
            result["overall"] = "FAIL"
    for case in ("runtime_800", "runtime_1600"):
        entry = result["runs"].get(case, {})
        if entry.get("divisor_changes") != 2 or entry.get("resizes") != 2:
            result["overall"] = "FAIL"
        if entry.get("shutdown_generation") != 5:
            result["overall"] = "FAIL"
    result["totals"] = {
        "runs": sum(1 for e in result["runs"].values() if e.get("exists")),
        "expected": len(CASES),
        "d3d12_error_lines": errors,
        "d3d12_warning_lines": warnings,
        "live_object_warning_lines": live,
    }
    result["transition_gpu_validation"] = {
        "status": "NOT RUN",
        "reason": (
            "The CloudRayMarchTransition shader under --gpu-validation hangs "
            "in the debug-layer shutdown path on the current machine. The "
            "unmodified R2 and R3 control binaries reproduce the same hang, "
            "so it is environment/driver drift, not an R4 regression."),
        "controls": "evidence/gbv_transition_control/summary.json",
        "debug_layer_run": "transition_debug",
    }
    OUT_JSON.write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    lines = ["V-WEATHER-01C-B-R4 GBV / InfoQueue", "=" * 44, ""]
    lines.append(f"Overall: {result['overall']}")
    lines.append(f"Runs: {result['totals']['runs']}/{len(CASES)}")
    lines.append(
        f"D3D12 errors: {errors}; warnings: {warnings} "
        f"(live-object: {live})")
    lines.append(
        "Transition shader GPU validation: NOT RUN "
        "(environment hang; R2/R3 controls reproduce; see "
        "evidence/gbv_transition_control)")
    for case, entry in result["runs"].items():
        lines.append(
            f"{case}: preflight={entry.get('preflight')} "
            f"debug_errors={entry.get('debug_errors')} "
            f"debug_warnings={entry.get('debug_warnings')} "
            f"divisor_changes={entry.get('divisor_changes')} "
            f"resizes={entry.get('resizes')} "
            f"shutdown_gen={entry.get('shutdown_generation')} "
            f"non_device_live={entry.get('non_device_live_objects')}")
    OUT_TXT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(result["overall"], errors, warnings)
    return 0 if result["overall"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())




