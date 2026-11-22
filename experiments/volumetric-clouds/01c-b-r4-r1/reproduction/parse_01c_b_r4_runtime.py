#!/usr/bin/env python3
"""Verify V-WEATHER-01C-B runtime settings state-machine evidence."""

from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work")
E = ROOT / "evidence"
OUT_JSON = E / "01c_b_r4_runtime_summary.json"
OUT_TXT = E / "01c_b_r4_runtime_summary.txt"

STAGE_RE = re.compile(
    r"runtime weather state: stage=(?P<stage>\S+) "
    r"timeline_active=(?P<timeline_active>\d) "
    r"timeline_time=(?P<timeline_time>[0-9.]+) "
    r"transition_active=(?P<transition_active>\d) "
    r"transition_time=(?P<transition_time>[0-9.]+) "
    r"cloud_time=(?P<cloud_time>[0-9.]+) "
    r"paused=(?P<paused>\d) weather_seed=(?P<weather_seed>\d+) "
    r"noise_seed=(?P<noise_seed>\d+) wind_speed=(?P<wind>[0-9.]+) "
    r"clouds_enabled=(?P<clouds>\d) quality=(?P<quality>\w+) "
    r"settings_generation=(?P<settings_gen>\d+) "
    r"resource_generation=(?P<resource_gen>\d+)")
APPLIED_RE = re.compile(
    r"cloud settings applied: reason=(?P<reason>\S+) "
    r"clouds_enabled=(?P<clouds>\d) quality=(?P<quality>\w+) "
    r"render_mode=(?P<mode>\w+) steps=(?P<steps>\d+) "
    r"shadows=(?P<shadows>\d+) divisor=(?P<divisor>\d+) "
    r"output=(?P<width>\d+)x(?P<height>\d+) format=(?P<format>\w+) "
    r"resource_generation=(?P<gen>\d+)")
REQUEST_RE = re.compile(
    r"runtime settings stage requested: stage=(?P<stage>\S+) "
    r"result=(?P<result>\w+)")
DIVISOR_RE = re.compile(
    r"cloud output divisor changed: divisor=(?P<divisor>\d+) "
    r"output=(?P<width>\d+)x(?P<height>\d+), generation=(?P<gen>\d+)")
RESIZE_RE = re.compile(
    r"cloud output resized: (?P<width>\d+)x(?P<height>\d+), "
    r"generation=(?P<gen>\d+)")
PAUSE_RE = re.compile(r"runtime pause check:.*unchanged=(?P<unchanged>\d)")
RESUME_RE = re.compile(r"runtime resume check:.*advanced=(?P<advanced>\d)")
COMPLETE_RE = re.compile(
    r"runtime settings test complete: generation=(?P<gen>\d+) "
    r"resource_generation=(?P<rgen>\d+)")
WEATHER_RE = re.compile(
    r"weather controller state: source=(?P<source>\S+) "
    r"mode=(?P<mode>\w+) timeline_active=(?P<active>\d) "
    r"timeline_time=(?P<time>[0-9.]+) transition_active=(?P<tr_active>\d) "
    r"transition_time=(?P<tr_time>[0-9.]+) cloud_time=(?P<cloud_time>[0-9.]+) "
    r"clouds_enabled=(?P<clouds>\d)")

QUALITY = {
    "performance": (64, 2, 2),
    "balanced": (96, 4, 2),
    "quality": (128, 6, 2),
    "reference": (128, 6, 1),
}
EXPECTED_STAGES = [
    "00_initial",
    "01_performance",
    "02_balanced",
    "03_quality",
    "04_reference",
    "05_balanced",
    "06_clouds_off",
    "07_clouds_on",
    "08_paused",
    "09_resumed",
    "10_resize_960x540",
    "11_resize_back",
    "12_quality_after_resize",
]
EXPECTED_QUALITY = [
    "quality", "performance", "balanced", "quality", "reference",
    "balanced", "balanced", "balanced", "balanced", "balanced",
    "balanced", "balanced", "quality",
]


def parse_run(case: str) -> dict:
    log = E / case / "run.log"
    text = log.read_text(encoding="utf-8", errors="replace")
    stages = []
    for match in STAGE_RE.finditer(text):
        row = match.groupdict()
        for key in ("timeline_active", "transition_active", "paused",
                    "weather_seed", "noise_seed", "clouds", "settings_gen",
                    "resource_gen"):
            row[key] = int(row[key])
        for key in ("timeline_time", "transition_time", "cloud_time", "wind"):
            row[key] = float(row[key])
        stages.append(row)
    applied = []
    for match in APPLIED_RE.finditer(text):
        row = match.groupdict()
        for key in ("clouds", "steps", "shadows", "divisor", "width",
                    "height", "gen"):
            row[key] = int(row[key])
        applied.append(row)
    requested = [m.groupdict() for m in REQUEST_RE.finditer(text)]
    divisors = []
    for match in DIVISOR_RE.finditer(text):
        row = match.groupdict()
        for key in ("divisor", "width", "height", "gen"):
            row[key] = int(row[key])
        divisors.append(row)
    resizes = []
    for match in RESIZE_RE.finditer(text):
        row = match.groupdict()
        for key in ("width", "height", "gen"):
            row[key] = int(row[key])
        resizes.append(row)
    pause = PAUSE_RE.search(text)
    resume = RESUME_RE.search(text)
    complete = COMPLETE_RE.search(text)
    return {
        "case": case,
        "stages": stages,
        "applied": applied,
        "requested": requested,
        "divisor_changes": divisors,
        "resizes": resizes,
        "pause_unchanged": int(pause.group("unchanged")) if pause else None,
        "resume_advanced": int(resume.group("advanced")) if resume else None,
        "complete": complete.groupdict() if complete else None,
    }


def verify_sequence(data: dict, width: int, height: int) -> list[str]:
    failures = []
    stages = data["stages"]
    if [s["stage"] for s in stages] != EXPECTED_STAGES:
        failures.append(f"{data['case']}: stage order mismatch")
        return failures
    if len(data["requested"]) != 8 or len(data["applied"]) != 8:
        failures.append(f"{data['case']}: settings transition count mismatch")
        return failures
    applied_by_stage = {
        request["stage"]: applied
        for request, applied in zip(data["requested"], data["applied"])
    }
    for index, stage in enumerate(stages):
        expected_quality = EXPECTED_QUALITY[index]
        if stage["quality"] != expected_quality:
            failures.append(
                f"{data['case']}/{stage['stage']}: quality "
                f"{stage['quality']} != {expected_quality}")
        steps, shadows, divisor = QUALITY[expected_quality]
        if stage["stage"] in applied_by_stage:
            applied = applied_by_stage[stage["stage"]]
            expected_clouds = 0 if stage["stage"] == "06_clouds_off" else 1
            if (applied["quality"] != expected_quality or
                    applied["steps"] != steps or
                    applied["shadows"] != shadows or
                    applied["divisor"] != divisor or
                    applied["clouds"] != expected_clouds):
                failures.append(
                    f"{data['case']}/{stage['stage']}: applied mismatch "
                    f"{applied}")
            expected_width = width // divisor
            expected_height = height // divisor
            if (applied["width"] != expected_width or
                    applied["height"] != expected_height):
                failures.append(
                    f"{data['case']}/{stage['stage']}: output "
                    f"{applied['width']}x{applied['height']} != "
                    f"{expected_width}x{expected_height}")
        expected_gen = 1
        if expected_quality == "reference":
            expected_gen = 2
        elif index >= 5:
            expected_gen = 3
        if stage["stage"] == "10_resize_960x540":
            expected_gen = 4
        elif stage["stage"] in ("11_resize_back",
                                "12_quality_after_resize"):
            expected_gen = 5
        if stage["resource_gen"] != expected_gen:
            failures.append(
                f"{data['case']}/{stage['stage']}: resource gen "
                f"{stage['resource_gen']} != {expected_gen}")
    if len(data["resizes"]) != 2:
        failures.append(f"{data['case']}: resize count mismatch")
    else:
        first, second = data["resizes"]
        if (first["width"], first["height"], first["gen"]) != (480, 270, 4):
            failures.append(f"{data['case']}: first resize mismatch")
        if ((second["width"], second["height"], second["gen"]) !=
                (width // 2, height // 2, 5)):
            failures.append(f"{data['case']}: second resize mismatch")
    if len(data["divisor_changes"]) != 2:
        failures.append(f"{data['case']}: divisor change count mismatch")
    else:
        first, second = data["divisor_changes"]
        if (first["divisor"], first["gen"]) != (1, 2) or \
                (second["divisor"], second["gen"]) != (2, 3):
            failures.append(f"{data['case']}: divisor generation mismatch")
    if data["complete"] is None or data["complete"]["gen"] != "8" or \
            data["complete"]["rgen"] != "5":
        failures.append(f"{data['case']}: completion generation mismatch")
    return failures


def verify_fixed(data: dict) -> list[str]:
    failures = verify_sequence(data, 800, 450)
    stages = data["stages"]
    for stage in stages:
        if (stage["timeline_time"] != 1.0 or stage["cloud_time"] != 0.0 or
                stage["weather_seed"] != 7331 or stage["noise_seed"] != 1337 or
                stage["wind"] != 0.0 or stage["transition_active"] != 0):
            failures.append(f"{data['case']}/{stage['stage']}: "
                            "weather state changed in fixed run")
        expected_clouds = 0 if stage["stage"] == "06_clouds_off" else 1
        if stage["clouds"] != expected_clouds:
            failures.append(f"{data['case']}/{stage['stage']}: clouds flag")
        expected_paused = 1 if stage["stage"] == "08_paused" else 0
        if stage["paused"] != expected_paused:
            failures.append(f"{data['case']}/{stage['stage']}: pause flag")
    if data["pause_unchanged"] != 1:
        failures.append(f"{data['case']}: pause did not freeze")
    return failures


def verify_animated(data: dict) -> list[str]:
    failures = verify_sequence(data, 800, 450)
    stages = {s["stage"]: s for s in data["stages"]}
    ordered = [stages[name] for name in EXPECTED_STAGES]
    for previous, current in zip(ordered, ordered[1:]):
        if current["stage"] == "08_paused":
            if current["timeline_time"] != previous["timeline_time"] or \
                    current["cloud_time"] != previous["cloud_time"]:
                failures.append("animated: pause advanced time")
        elif current["stage"] == "09_resumed":
            if current["timeline_time"] <= previous["timeline_time"] or \
                    current["cloud_time"] <= previous["cloud_time"]:
                failures.append("animated: resume did not advance")
        else:
            if current["timeline_time"] < previous["timeline_time"] or \
                    current["cloud_time"] < previous["cloud_time"]:
                failures.append(
                    f"animated: time reset at {current['stage']}")
    if data["pause_unchanged"] != 1:
        failures.append("animated: pause check failed")
    if data["resume_advanced"] != 1:
        failures.append("animated: resume check failed")
    if data["complete"] is None:
        failures.append("animated: completion line missing")
    return failures


def main() -> int:
    fixed = parse_run("runtime_fixed")
    animated = parse_run("runtime_animated")
    wide = parse_run("runtime_fixed_1600")
    failures = []
    failures += verify_fixed(fixed)
    failures += verify_animated(animated)
    wide_failures = verify_sequence(wide, 1600, 900)
    failures += wide_failures

    timeline = {}
    for mode in ("on", "off"):
        text = (E / f"timeline_compare_{mode}" / "run.log").read_text(
            encoding="utf-8", errors="replace")
        matches = list(WEATHER_RE.finditer(text))
        if not matches:
            failures.append(f"timeline {mode}: state missing")
            continue
        entry = matches[-1].groupdict()
        for key in ("active", "clouds"):
            entry[key] = int(entry[key])
        for key in ("time", "tr_time", "cloud_time"):
            entry[key] = float(entry[key])
        timeline[mode] = entry
    if len(timeline) == 2:
        on = dict(timeline["on"])
        off = dict(timeline["off"])
        on.pop("clouds", None)
        off.pop("clouds", None)
        if on != off:
            failures.append(f"timeline on/off mismatch: {on} vs {off}")
    else:
        failures.append("timeline on/off evidence missing")

    payload = {
        "format": "V_WEATHER_01C_B_R4_RUNTIME_STATE_MACHINE_V1",
        "date": "2026-11-20",
        "result": "PASS" if not failures else "FAIL",
        "fixed": fixed,
        "animated": animated,
        "wide": wide,
        "timeline_compare": timeline,
        "failures": failures,
    }
    OUT_JSON.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    lines = ["V-WEATHER-01C-B runtime settings state machine", "=" * 58, ""]
    lines.append(f"Result: {payload['result']}")
    for label, data in (("fixed 800x450", fixed),
                        ("animated 800x450", animated),
                        ("fixed 1600x900", wide)):
        lines.append(f"== {label} ==")
        lines.append(
            f"{'stage':24} {'quality':12} {'steps':>5} {'shadows':>7} "
            f"{'div':>4} {'gen':>4} {'time':>8} {'cloud':>8}")
        for stage in data["stages"]:
            lines.append(
                f"{stage['stage']:24} {stage['quality']:12} "
                f"{'-':>5} {'-':>7} {'-':>4} "
                f"{stage['resource_gen']:4d} {stage['timeline_time']:8.4f} "
                f"{stage['cloud_time']:8.4f}")
        lines.append(
            f"pause_unchanged={data['pause_unchanged']} "
            f"resume_advanced={data['resume_advanced']}")
    lines.append("== timeline on/off ==")
    lines.append(str(timeline))
    if failures:
        lines.append("FAILURES:")
        lines.extend(" - " + failure for failure in failures)
    OUT_TXT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(payload["result"], "failures", len(failures))
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())



