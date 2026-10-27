#!/usr/bin/env python3
"""Fault-injection harness for verify_physx_smoke_multi_reload.py.

The harness runs the strict validator on the accepted runtime log (must PASS),
then on eight deliberately corrupted copies (each must FAIL for a specific
reason). stdout, stderr and the real exit code of every run are written to the
output directory.

Usage:
  python fault_inject_physx_smoke_validator.py <accepted.log> <output-dir>
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
VALIDATOR = HERE / "verify_physx_smoke_multi_reload.py"

PREFIX_RE = re.compile(r"^(\[[^\]]+\] \[\d+\] )")


def prefix_of(line: str) -> str:
    match = PREFIX_RE.match(line)
    if not match:
        raise ValueError(f"cannot parse log prefix: {line!r}")
    return match.group(1)


def find_lines(lines: list[str], pattern: str) -> list[int]:
    regex = re.compile(pattern)
    return [index for index, line in enumerate(lines) if regex.search(line)]


def remove_nth(lines: list[str], pattern: str, nth: int) -> list[str]:
    indices = find_lines(lines, pattern)
    if len(indices) < nth:
        raise ValueError(f"pattern not found {nth} time(s): {pattern}")
    del lines[indices[nth - 1]]
    return lines


def replace_in_nth(lines: list[str], pattern: str, nth: int, old: str, new: str) -> list[str]:
    indices = find_lines(lines, pattern)
    if len(indices) < nth:
        raise ValueError(f"pattern not found {nth} time(s): {pattern}")
    index = indices[nth - 1]
    if old not in lines[index]:
        raise ValueError(f"{old!r} not in line: {lines[index]!r}")
    lines[index] = lines[index].replace(old, new, 1)
    return lines


def insert_after_nth(lines: list[str], pattern: str, nth: int, body: str) -> list[str]:
    indices = find_lines(lines, pattern)
    if len(indices) < nth:
        raise ValueError(f"pattern not found {nth} time(s): {pattern}")
    anchor = indices[nth - 1]
    lines.insert(anchor + 1, prefix_of(lines[anchor]) + body)
    return lines


def append_line(lines: list[str], body: str) -> list[str]:
    lines.append(prefix_of(lines[-1]) + body)
    return lines


def run_validator(log_path: Path) -> tuple[int, str, str]:
    completed = subprocess.run(
        [sys.executable, str(VALIDATOR), str(log_path)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    return completed.returncode, completed.stdout, completed.stderr


def save_run(out_dir: Path, name: str, code: int, stdout: str, stderr: str) -> None:
    (out_dir / f"{name}.stdout.txt").write_text(stdout, encoding="utf-8")
    (out_dir / f"{name}.stderr.txt").write_text(stderr, encoding="utf-8")
    (out_dir / f"{name}.exit.txt").write_text(f"{code}\n", encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 3:
        print(
            "usage: fault_inject_physx_smoke_validator.py <accepted.log> <output-dir>",
            file=sys.stderr,
        )
        return 2

    source = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    faults_dir = out_dir / "faults"
    faults_dir.mkdir(parents=True, exist_ok=True)

    if not source.is_file():
        print(f"ERROR: log not found: {source}", file=sys.stderr)
        return 2
    if not VALIDATOR.is_file():
        print(f"ERROR: validator not found: {VALIDATOR}", file=sys.stderr)
        return 2

    base_lines = source.read_text(encoding="utf-8", errors="replace").splitlines()

    code, stdout, stderr = run_validator(source)
    save_run(out_dir, "strict-original", code, stdout, stderr)
    print(f"original: exit={code} expected=0 -> {'PASS' if code == 0 else 'FAIL'}")
    ok = code == 0

    faults = [
        (
            "fault-01-drop-result-session3",
            "session.3.sequence",
            lambda lines: remove_nth(lines, r"multi result second=", 3),
        ),
        (
            "fault-02-second-not-ode",
            "session.2.result",
            lambda lines: replace_in_nth(
                lines,
                r"multi result second=",
                2,
                "second=1 shell=1 third=1",
                "second=1 shell=0 third=1",
            ),
        ),
        (
            "fault-03-slot-not-reused",
            "session.1.third.reuse",
            lambda lines: replace_in_nth(
                lines, r"multi third hit id=", 1, "physx_instance_before=0", "physx_instance_before=1"
            ),
        ),
        (
            "fault-04-owner-release-missing",
            "ownership.balance",
            lambda lines: remove_nth(
                lines, r"releasing PhysX owner state for \[sim_default_stalker_26525\]", 3
            ),
        ),
        (
            "fault-05-third-reload",
            "reload.cap",
            lambda lines: insert_after_nth(
                lines, r"multi result second=", 3, "ph_runtime_smoke: ragdoll reload 3/2 requested"
            ),
        ),
        (
            "fault-06-bad-body-count",
            "creation.geometry",
            lambda lines: replace_in_nth(
                lines, r"PhysX ragdoll created bodies=", 1, "bodies=11 joints=10", "bodies=10 joints=9"
            ),
        ),
        (
            "fault-07-crash-marker",
            "crash.markers",
            lambda lines: append_line(lines, "Unhandled exception: injected fault"),
        ),
        (
            "fault-08-reset-missing",
            "reset.tick1",
            lambda lines: remove_nth(lines, r"ph_ragdoll_smoke: tick frame=1\s*$", 3),
        ),
    ]

    for name, expected_id, mutate in faults:
        lines = list(base_lines)
        mutate(lines)
        log_path = faults_dir / f"{name}.log"
        log_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        code, stdout, stderr = run_validator(log_path)
        save_run(out_dir, name, code, stdout, stderr)
        observed = code != 0 and any(
            line.startswith("FAIL: ") and expected_id in line for line in stdout.splitlines()
        )
        print(
            f"{name}: exit={code} expected_fail={expected_id} -> "
            f"{'PASS' if observed else 'FAIL'}"
        )
        ok = ok and observed

    print()
    if not ok:
        print("FAILED: fault-injection expectations were not met")
        return 1
    print("PASS: original accepted, all eight corrupted logs rejected")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
