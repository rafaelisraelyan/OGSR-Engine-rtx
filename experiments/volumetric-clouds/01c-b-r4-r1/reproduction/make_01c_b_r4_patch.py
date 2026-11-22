#!/usr/bin/env python3
"""Build the V-WEATHER-01C-B-R4-R1 incremental patch.

The baseline is the exact rebuilt R4 standalone tree (zip SHA-256
bfc4bdd5d74da39226617a2093981e5819d207f7d6ae67d6aedda69a7d5151c8); the patch
turns it into the R4-R1 candidate. Comparison and apply-check are byte-exact:
the tree intentionally mixes CRLF and LF files, so any normalisation would make
the patch non-applicable to a clean baseline checkout.
"""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work")
BASE = Path(r"C:\Users\GR\AppData\Local\OGSR_V_WEATHER_01A_WORK\01c_b_r4_r1_work\standalone_01c_b_r4_r1")
CUR = ROOT / "standalone_01c_b_r4_r1"
PKG = ROOT / "patch"
TMP = PKG / "patch_tmp"
OUT = PKG / "V_WEATHER_01C_B_R4_R1_Interactive_Diagnostic.patch"
APPLY = PKG / "patch_apply_test"
FILES = [
    "src/Renderer.h",
    "src/Renderer.cpp",
    "src/Application.cpp",
    "src/CloudFaultInjection.h",
    "src/CloudFaultInjection.cpp",
    "src/CloudWitness.h",
    "src/NoiseVolume.cpp",
    "src/WeatherMap.cpp",
    "src/CloudRayMarcher.cpp",
    "tests/terminal_witness_harness.cpp",
]
GIT = ["git", "-c", "core.autocrlf=false", "-c", "core.eol=lf",
       "-c", "core.safecrlf=false"]


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    PKG.mkdir(parents=True, exist_ok=True)
    if TMP.exists():
        shutil.rmtree(TMP)
    (TMP / "a").mkdir(parents=True)
    (TMP / "b").mkdir(parents=True)
    manifest = {
        "format": "V_WEATHER_01C_B_R4_R1_PATCH_MANIFEST_V1",
        "baseline": str(BASE),
        "baseline_zip_sha256":
            "bfc4bdd5d74da39226617a2093981e5819d207f7d6ae67d6aedda69a7d5151c8",
        "current": str(CUR),
        "eol_policy": "byte-exact, no normalisation",
        "files": [],
    }
    for rel in FILES:
        base_path = BASE / rel
        cur_path = CUR / rel
        if not cur_path.exists():
            raise SystemExit(f"missing current file: {rel}")
        a = TMP / "a" / rel
        b = TMP / "b" / rel
        a.parent.mkdir(parents=True, exist_ok=True)
        b.parent.mkdir(parents=True, exist_ok=True)
        if base_path.exists():
            a.write_bytes(base_path.read_bytes())
        b.write_bytes(cur_path.read_bytes())
        manifest["files"].append({
            "path": rel,
            "base_exists": base_path.exists(),
            "base_sha256": sha(base_path) if base_path.exists() else None,
            "current_sha256": sha(cur_path),
            "current_size": cur_path.stat().st_size,
        })
    result = subprocess.run(
        GIT + ["diff", "--no-index", "--src-prefix=a/", "--dst-prefix=b/",
               "a", "b"],
        cwd=TMP, capture_output=True)
    patch = result.stdout.decode("utf-8", errors="replace")
    patch = patch.replace("a/a/", "a/").replace("b/b/", "b/")
    patch = patch.replace("diff --git a/b/", "diff --git a/")
    if not patch:
        raise SystemExit("empty patch")
    OUT.write_text(patch, encoding="utf-8", newline="\n")
    manifest["patch_sha256"] = sha(OUT)
    manifest["patch_lines"] = len(patch.splitlines())
    if APPLY.exists():
        shutil.rmtree(APPLY)
    shutil.copytree(BASE, APPLY)
    check = subprocess.run(GIT + ["apply", "-p1", "--check", str(OUT)],
                           cwd=APPLY, capture_output=True)
    if check.returncode != 0:
        raise SystemExit("patch --check failed:\n" +
                         check.stderr.decode("utf-8", "replace"))
    applied = subprocess.run(GIT + ["apply", "-p1", str(OUT)], cwd=APPLY,
                             capture_output=True)
    if applied.returncode != 0:
        raise SystemExit("patch apply failed:\n" +
                         applied.stderr.decode("utf-8", "replace"))
    mismatches = [rel for rel in FILES
                  if (APPLY / rel).read_bytes() != (CUR / rel).read_bytes()]
    manifest["apply_check"] = "PASS" if not mismatches else "FAIL"
    manifest["apply_mismatches"] = mismatches
    manifest["apply_mode"] = "byte-for-byte, core.autocrlf=false"
    (PKG / "patch_manifest_r4_r1.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    print(f"patch={OUT} lines={manifest['patch_lines']} "
          f"sha256={manifest['patch_sha256']}")
    print(f"apply={manifest['apply_check']} mismatches={mismatches}")
    return 0 if not mismatches else 1


if __name__ == "__main__":
    raise SystemExit(main())
