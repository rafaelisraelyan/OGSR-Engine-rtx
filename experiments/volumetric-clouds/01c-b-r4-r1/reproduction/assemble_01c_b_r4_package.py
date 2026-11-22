#!/usr/bin/env python3
"""Assemble the V-WEATHER-01C-B-R3 evidence package."""

from __future__ import annotations

import hashlib
import json
import shutil
import zipfile
from pathlib import Path

ROOT = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work")
SRC = ROOT / "standalone_01c_b_r4_r1"
PKG = ROOT / "package"
REPORT = ROOT / "report"
PATCH = ROOT / "patch"
EVIDENCE = ROOT / "evidence"
ZIP = ROOT / "OGSR_V_WEATHER_01C_B_R4_R1_Interactive_Diagnostic_Corrections.zip"
ZIP_SHA = ZIP.with_suffix(".zip.sha256")

REQUIRED_SOURCE = [
    "src/Renderer.cpp", "src/Renderer.h",
    "src/Common.cpp", "src/Common.h",
    "src/CloudRayMarcher.cpp", "src/CloudRayMarcher.h",
    "src/Application.cpp", "src/Application.h",
]
CHANGED_SOURCE = REQUIRED_SOURCE + [
    "src/CloudFaultInjection.cpp", "src/CloudFaultInjection.h",
    "src/WeatherMap.cpp", "src/NoiseVolume.cpp", "src/CloudWitness.h",
    "tests/terminal_witness_harness.cpp",
]
NEEDLES = [
    "OGSR_CLOUD_LOGGER_FAULT",
    "OGSR_CLOUD_FAULT_POINT",
    "OGSR_CLOUD_FAULT_PROBE_CAPACITY",
    "OGSR_CLOUD_WITNESS_SHM",
    "OGSR_CLOUD_PIPESTATUS_SHM",
    "OGSR_CLOUD_BLOCK_AT_LABEL",
    "OGSR_CLOUD_TEST_SUPPRESS_LOGGING",
    "OGSR_CLOUD_BLOCK_PROBE_TARGET",
    "OGSR_CLOUD_INFLIGHT_TEST",
    "OGSR_CLOUD_FORCE_PRESENT_NOTICE",
]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def copy_text_normalized(src: Path, dst: Path) -> None:
    data = src.read_bytes()
    if data.startswith(b"\xff\xfe") or data.startswith(b"\xfe\xff"):
        text = data.decode("utf-16")
    else:
        text = data.decode("utf-8", errors="replace")
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text(text, encoding="utf-8")


def copy_logs(src_dir: Path, dst_dir: Path, patterns=(
        "stdout.txt", "stderr.txt", "witness.json", "pipe_status.json",
        "exit_code.txt", "timed_out.txt", "environment.txt")) -> None:
    if not src_dir.exists():
        return
    for name in patterns:
        path = src_dir / name
        if path.exists():
            copy_file(path, dst_dir / name)


def main() -> int:
    if PKG.exists():
        shutil.rmtree(PKG)
    PKG.mkdir(parents=True)

    ignore = shutil.ignore_patterns("__pycache__", "*.pyc")
    shutil.copytree(SRC, PKG / "source", ignore=ignore)

    for rel in REQUIRED_SOURCE:
        copy_file(SRC / rel, PKG / "source_required" / Path(rel).name)

    for name in ("README.md", "REPORT.md", "WORKSPACE_MANIFEST.md",
                 "NO_PRODUCTION_CHANGE.txt", "exact_commands.txt",
                 "environment.txt"):
        copy_file(REPORT / name, PKG / name)

    copy_file(PATCH / "V_WEATHER_01C_B_R4_R1_Interactive_Diagnostic.patch",
              PKG / "patch" / "V_WEATHER_01C_B_R4_R1_Interactive_Diagnostic.patch")
    copy_file(PATCH / "patch_manifest_r4_r1.json",
              PKG / "patch" / "patch_manifest_r4_r1.json")

    for name in ("01c_b_r4_faults.json", "01c_b_r4_faults.txt",
                 "01c_b_r4_static_identity.json", "01c_b_r4_static_identity.txt",
                 "01c_b_r4_vs_r2_static.json", "01c_b_r4_vs_r2_static.txt",
                 "01c_b_r4_static_drift.json",
                 "01c_b_r4_historical_drift.json",
                 "01c_b_r4_historical_drift.txt",
                 "01c_b_r4_runtime_summary.json", "01c_b_r4_runtime_summary.txt",
                 "01c_b_r4_gbv_summary.json", "01c_b_r4_gbv_summary.txt"):
        path = EVIDENCE / name
        if path.exists():
            copy_file(path, PKG / "evidence" / name)

    drift_dir = EVIDENCE / "static_drift"
    if drift_dir.exists():
        for path in sorted(drift_dir.glob("*.bmp")):
            copy_file(path, PKG / "evidence" / "static_drift" / path.name)

    baseline = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\package_01c_a_r1\static")
    if baseline.exists():
        for path in sorted(baseline.rglob("*.png")):
            rel = path.relative_to(baseline)
            copy_file(path, PKG / "evidence" / "historical_baseline_static" / rel)

    # Static and runtime PNGs plus their textual logs; BMPs are regenerable
    # intermediates and are intentionally excluded.
    for family in ("static", "r2_current_static", "runtime_fixed",
                   "runtime_animated", "runtime_fixed_1600",
                   "timeline_compare_on", "timeline_compare_off"):
        src_dir = EVIDENCE / family
        if not src_dir.exists():
            continue
        for path in src_dir.rglob("*"):
            if path.is_file() and (path.suffix.lower() in (".png", ".csv")
                                   or path.name in ("run.log", "run.err.log")):
                rel = path.relative_to(EVIDENCE)
                copy_file(path, PKG / "evidence" / rel)

    # Fault logs only, not the large per-stage BMPs.
    faults_dir = EVIDENCE / "faults"
    if faults_dir.exists():
        for case_dir in sorted(faults_dir.iterdir()):
            if case_dir.is_dir():
                copy_logs(case_dir, PKG / "evidence" / "faults" / case_dir.name)

    # CLI and GBV logs.
    cli_dir = EVIDENCE / "cli"
    if cli_dir.exists():
        for path in cli_dir.iterdir():
            if path.is_file() and path.suffix.lower() in (".csv", ".log"):
                copy_file(path, PKG / "evidence" / "cli" / path.name)
    gbv_dir = EVIDENCE / "gbv"
    if gbv_dir.exists():
        for case_dir in sorted(gbv_dir.iterdir()):
            if case_dir.is_dir():
                copy_logs(case_dir, PKG / "evidence" / "gbv" / case_dir.name,
                          ("run.log", "run.err.log"))
    # R4-R1: the transition/GBV investigation is its own evidence tree with
    # per-experiment logs, a summary and a reproduction plan.
    investigation_dir = EVIDENCE / "gbv_transition_investigation"
    if investigation_dir.is_dir():
        for path in sorted(investigation_dir.rglob("*")):
            if path.is_file():
                rel = path.relative_to(EVIDENCE)
                copy_file(path, PKG / "evidence" / rel)
    # Independent rechecks of the cases that were disturbed by GPU contention.
    recheck_dir = EVIDENCE / "recheck"
    if recheck_dir.is_dir():
        for path in sorted(recheck_dir.rglob("*")):
            if path.is_file():
                rel = path.relative_to(EVIDENCE)
                copy_file(path, PKG / "evidence" / rel)
    control_dir = EVIDENCE / "gbv_transition_control"
    if control_dir.exists():
        for path in sorted(control_dir.rglob("*")):
            if path.is_file():
                rel = path.relative_to(control_dir)
                copy_file(path, PKG / "evidence" / "gbv_transition_control" / rel)

    # Build and run logs.
    val = PKG / "validation"
    val.mkdir()
    for name in ("cmake_developer.log", "cmake_shipping.log", "cmake_fault.log",
                 "build_developer.log", "build_shipping.log", "build_fault.log",
                 "ctest_developer.log", "ctest_shipping.log", "ctest_fault.log",
                 "run_static.log", "run_runtime.log", "run_faults.log",
                 "run_cli.log", "run_gbv.log", "run_r2_static.log",
                 "run_gbv_transition_control.log",
                 "source_evidence.txt", "source_evidence.json"):
        path = ROOT / name
        if not path.exists():
            path = ROOT / "validation" / name
        if path.exists():
            copy_text_normalized(path, val / name)

    binaries = {
        "developer": ROOT / "build_developer" / "Release" / "OGSRCloudsTest.exe",
        "shipping": ROOT / "build_shipping" / "Release" / "OGSRCloudsTest.exe",
        "fault": ROOT / "build_fault" / "Release" / "OGSRCloudsTest.exe",
    }
    isolation = {"format": "V_WEATHER_01C_B_R4_R1_HOOK_ISOLATION_V1",
                 "result": "PASS", "binaries": {}, "hashes": {}}
    exe_lines = []
    for build, path in binaries.items():
        if not path.exists():
            raise SystemExit(f"missing binary: {path}")
        data = path.read_bytes()
        present = {needle: (needle.encode() in data) for needle in NEEDLES}
        isolation["binaries"][build] = present
        isolation["hashes"][build] = sha(path)
        exe_lines.append(f"{sha(path)}  {build}  {path.name}")
        copy_file(path, val / "binaries" / f"OGSRCloudsTest_{build}.exe")
        if build in ("developer", "shipping") and any(present.values()):
            isolation["result"] = "FAIL"
        if build == "fault" and not all(present.values()):
            isolation["result"] = "FAIL"
    (val / "exe_sha256.txt").write_text(
        "\n".join(exe_lines) + "\n", encoding="utf-8")
    harness = ROOT / "build_fault" / "Release" / "OGSRCloudsTerminalHarness.exe"
    if not harness.exists():
        raise SystemExit(f"missing harness: {harness}")
    copy_file(harness, val / "binaries" / "OGSRCloudsTerminalHarness.exe")
    with open(val / "exe_sha256.txt", "a", encoding="utf-8") as stream:
        stream.write(f"{sha(harness)}  harness  {harness.name}\n")

    # Unmodified R2 control binary: shipped so the 16/16 differential can be
    # reproduced from the package without access to the author's machine.
    r2_control = Path(
        r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r2_work"
        r"\build_developer\Release\OGSRCloudsTest.exe")
    if not r2_control.exists():
        raise SystemExit(f"missing R2 control binary: {r2_control}")
    copy_file(r2_control, val / "binaries" / "OGSRCloudsTest_r2_control.exe")
    r2_data = r2_control.read_bytes()
    r2_present = {needle: (needle.encode() in r2_data) for needle in NEEDLES}
    isolation["binaries"]["r2_control"] = r2_present
    isolation["hashes"]["r2_control"] = sha(r2_control)
    if any(r2_present.values()):
        isolation["result"] = "FAIL"
    with open(val / "exe_sha256.txt", "a", encoding="utf-8") as stream:
        stream.write(f"{sha(r2_control)}  r2_control  {r2_control.name}\n")
    (val / "hook_isolation.json").write_text(
        json.dumps(isolation, indent=2) + "\n", encoding="utf-8")

    source_lines = []
    for path in sorted((PKG / "source").rglob("*")):
        if path.is_file() and "__pycache__" not in path.parts:
            rel = path.relative_to(PKG / "source").as_posix()
            source_lines.append(f"{sha(path)}  {rel}")
    (val / "source_sha256.txt").write_text(
        "\n".join(source_lines) + "\n", encoding="utf-8")

    shader_lines = []
    for path in sorted((PKG / "source" / "shaders").glob("*.hlsl")):
        shader_lines.append(f"{sha(path)}  {path.name}")
    (val / "shader_sha256.txt").write_text(
        "\n".join(shader_lines) + "\n", encoding="utf-8")

    copy_file(ROOT / "verify_01c_b_r4_package.py",
              PKG / "verify_01c_b_r4_package.py")

    reproduction = PKG / "reproduction"
    for name in (
        "build_canonical.ps1",
        "run_01c_b_r4_static.ps1", "run_r2_current_static.ps1",
        "run_01c_b_r4_runtime.ps1",
        "run_01c_b_r4_cli_tests.ps1", "run_01c_b_r4_gbv.ps1",
        "run_01c_b_r4_gbv_transition_control.ps1",
        "run_01c_b_r4_faults.ps1",
        "parse_01c_b_r4_static.py", "parse_01c_b_r4_runtime.py",
        "parse_01c_b_r4_faults.py", "summarize_01c_b_r4_gbv.py",
        "compare_r4_vs_r2_static.py", "make_static_drift.py",
        "compare_historical_baseline.py",
        "make_01c_b_r4_patch.py", "make_source_evidence.py",
        "assemble_01c_b_r4_package.py",
    ):
        path = ROOT / name
        if path.exists():
            copy_file(path, reproduction / name)

    # Written before the checksums so the record is itself covered by them.
    faults_doc = json.loads(
        (EVIDENCE / "01c_b_r4_faults.json").read_text(encoding="utf-8-sig"))
    (PKG / "package_verification.txt").write_text(
        "V-WEATHER-01C-B-R4-R1 package verification record\n"
        "=================================================\n"
        "Deliverable: OGSR_V_WEATHER_01C_B_R4_R1_"
        "Interactive_Diagnostic_Corrections.zip\n"
        "Baseline: rebuilt R4 package, SHA-256\n"
        "  bfc4bdd5d74da39226617a2093981e5819d207f7d6ae67d6aedda69a7d5151c8\n"
        f"Fault-matrix cases: {faults_doc['counts']['cases']}\n"
        f"Fault-matrix failures: {faults_doc['counts']['failures']}\n"
        f"Fault-matrix result: {faults_doc['result']}\n"
        f"Hook isolation: {isolation['result']}\n"
        "Binaries:\n"
        + "".join(
            f"  {name}: {value}\n"
            for name, value in isolation["hashes"].items())
        + "Patch: see patch/patch_manifest_r4_r1.json "
          "(byte-exact apply verified against a clean R4 tree)\n"
        "Verifier: run verify_01c_b_r4_package.py against the ZIP; it recomputes\n"
        "  every numeric claim from the packaged raw evidence.\n"
        "Author-only evidence: no independent rerun was performed.\n",
        encoding="utf-8")

    sums = []
    for path in sorted(PKG.rglob("*")):
        if path.is_file() and path.name != "SHA256SUMS.txt":
            rel = path.relative_to(PKG).as_posix()
            sums.append(f"{sha(path)}  {rel}")
    (PKG / "SHA256SUMS.txt").write_text(
        "\n".join(sums) + "\n", encoding="utf-8")

    if ZIP.exists():
        ZIP.unlink()
    with zipfile.ZipFile(ZIP, "w", zipfile.ZIP_DEFLATED,
                         compresslevel=9) as archive:
        for path in sorted(PKG.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(PKG).as_posix())
    digest = sha(ZIP)
    ZIP_SHA.write_text(
        f"{digest}  {ZIP.name}\n", encoding="utf-8")
    size_mb = ZIP.stat().st_size / (1024 * 1024)
    print(f"zip={ZIP} size={size_mb:.2f} MiB sha256={digest}")
    print(f"isolation={isolation['result']} files={len(sums)}")
    return 0 if isolation["result"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())


