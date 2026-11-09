#!/usr/bin/env python3
"""Strict validator for the P2.2A combined multi-NPC + reload smoke log.

The runtime contract encoded here is the accepted P2.2A FINAL ACCEPTANCE run
(three sessions, two reloads, no third reload):

  session = tick frame=1
            -> lethal hit of the PhysX target
            -> created bodies=11 joints=10 bones=47
            -> owner committed
            -> two extra stalkers spawned
            -> second NPC hit while a PhysX instance is active (ODE fallback)
            -> first PhysX NPC destroyed
            -> owner release + instance release
            -> third NPC hit with physx_instance_before=0 (slot reuse)
            -> created 11/10/47 + owner commit
            -> multi result second=1 shell=1 third=1 shell=0 physx_instance=1
            -> reload gate
            -> [sessions 1 and 2] reload N/2 + owner release + reloading level

The validator is read-only. It exits 0 only when every check passes.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

LINE_RE = re.compile(
    r"^\[(\d{2})\.(\d{2})\.(\d{2}) (\d{2}):(\d{2}):(\d{2})\.(\d{3})\] \[(\d+)\] (.*)$"
)

PATTERNS = (
    ("tick1", re.compile(r"ph_ragdoll_smoke: tick frame=1\s*$")),
    (
        "hit1",
        re.compile(
            r"ph_ragdoll_smoke: lethal hit target=\[([^ \]]+) / ([^\]]+)\] id=(\d+)"
        ),
    ),
    (
        "created",
        re.compile(
            r"ph_ragdoll: PhysX ragdoll created bodies=(\d+) joints=(\d+) bones=(\d+) "
            r"total_mass=([0-9.]+) root_mass=([0-9.]+)"
        ),
    ),
    (
        "commit",
        re.compile(
            r"ph_ragdoll: owner committed to PhysX name=\[([^\]]+)\] "
            r"section=\[([^\]]+)\] bodies=(\d+) joints=(\d+)"
        ),
    ),
    ("spawn", re.compile(r"ph_ragdoll_smoke: multi spawned two extra stalkers")),
    (
        "second",
        re.compile(r"ph_ragdoll_smoke: multi second hit id=(\d+) physx_instance=(\d+)"),
    ),
    ("destroy", re.compile(r"ph_ragdoll_smoke: multi destroying first PhysX NPC id=(\d+)")),
    ("release", re.compile(r"ph_ragdoll: releasing PhysX owner state for \[([^\]]+)\]")),
    ("instance_released", re.compile(r"ph_ragdoll: PhysX ragdoll instance released\s*$")),
    (
        "third",
        re.compile(r"ph_ragdoll_smoke: multi third hit id=(\d+) physx_instance_before=(\d+)"),
    ),
    (
        "result",
        re.compile(
            r"ph_ragdoll_smoke: multi result second=(\d+) shell=(\d+) third=(\d+) "
            r"shell=(\d+) physx_instance=(\d+)"
        ),
    ),
    ("reload_gate", re.compile(r"reload gate requesting level disconnect")),
    ("reload", re.compile(r"ph_runtime_smoke: ragdoll reload (\d+)/2 requested")),
    ("reloading", re.compile(r"ph_runtime_smoke: reloading level\s*$")),
)

EXPECTED_SESSION_12 = [
    "tick1",
    "hit1",
    "created",
    "commit",
    "spawn",
    "second",
    "destroy",
    "release",
    "instance_released",
    "third",
    "created",
    "commit",
    "result",
    "reload_gate",
    "reload",
    "release",
    "reloading",
]

EXPECTED_SESSION_3 = [
    "tick1",
    "hit1",
    "created",
    "commit",
    "spawn",
    "second",
    "destroy",
    "release",
    "instance_released",
    "third",
    "created",
    "commit",
    "result",
    "reload_gate",
    "release",
]

CRASH_MARKERS = (
    "Unhandled exception",
    "ScriptCrashHandler",
    "stack trace:",
    "FATAL ERROR",
    "assertion failed",
    "ACCESS_VIOLATION",
)


class Event:
    __slots__ = ("kind", "fields", "stamp", "line")

    def __init__(self, kind, fields, stamp, line):
        self.kind = kind
        self.fields = fields
        self.stamp = stamp
        self.line = line


def parse_events(lines):
    events = []
    for line in lines:
        match = LINE_RE.match(line)
        if not match:
            continue
        stamp = (
            int(match.group(3)),
            int(match.group(2)),
            int(match.group(1)),
            int(match.group(4)),
            int(match.group(5)),
            int(match.group(6)),
            int(match.group(7)),
        )
        body = match.group(9)
        for kind, pattern in PATTERNS:
            found = pattern.search(body)
            if found:
                events.append(Event(kind, found.groups(), stamp, line))
                break
    return events


def split_sessions(events):
    sessions = []
    current = None
    for event in events:
        if event.kind == "tick1":
            if current is not None:
                sessions.append(current)
            current = []
        if current is None:
            continue
        current.append(event)
    if current:
        sessions.append(current)
    return sessions


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: verify_physx_smoke_multi_reload.py <runtime.log>", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    if not path.is_file():
        print(f"FAIL: input.exists ({path})", file=sys.stderr)
        return 2

    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()
    events = parse_events(lines)
    sessions = split_sessions(events)

    failures: list[str] = []

    def check(check_id: str, condition: bool, detail: str = "") -> None:
        print(f"{'PASS' if condition else 'FAIL'}: {check_id}" + (f" ({detail})" if detail else ""))
        if not condition:
            failures.append(check_id)

    check("sessions.count", len(sessions) == 3, f"count={len(sessions)}")

    for index, session in enumerate(sessions, 1):
        expected = EXPECTED_SESSION_3 if index == 3 else EXPECTED_SESSION_12
        actual = [event.kind for event in session]
        check(f"session.{index}.sequence", actual == expected, f"actual={actual}")

    created = [event for event in events if event.kind == "created"]
    check("creation.count", len(created) == 6, f"count={len(created)}")
    check(
        "creation.geometry",
        all(event.fields[:3] == ("11", "10", "47") for event in created),
        f"values={[event.fields[:3] for event in created]}",
    )
    check(
        "creation.mass",
        all(event.fields[3:] == ("92.21", "9.88") for event in created),
        f"values={[event.fields[3:] for event in created]}",
    )

    commits = [event for event in events if event.kind == "commit"]
    releases = [event for event in events if event.kind == "release"]
    check("ownership.commits", len(commits) == 6, f"count={len(commits)}")
    check("ownership.releases", len(releases) == 6, f"count={len(releases)}")

    active = None
    ownership_ok = True
    ownership_detail = ""
    for event in events:
        if event.kind == "commit":
            if active is not None:
                ownership_ok = False
                ownership_detail = f"commit while owner={active}"
                break
            active = event.fields[0]
        elif event.kind == "release":
            if active is None:
                ownership_ok = False
                ownership_detail = f"release without owner ({event.fields[0]})"
                break
            if event.fields[0] != active:
                ownership_ok = False
                ownership_detail = f"release {event.fields[0]} != owner {active}"
                break
            active = None
    check("ownership.balance", ownership_ok, ownership_detail)

    instance_released = [event for event in events if event.kind == "instance_released"]
    check("instance.released", len(instance_released) == 3, f"count={len(instance_released)}")

    for index, session in enumerate(sessions, 1):
        hits = [event for event in session if event.kind == "hit1"]
        seconds = [event for event in session if event.kind == "second"]
        thirds = [event for event in session if event.kind == "third"]
        destroys = [event for event in session if event.kind == "destroy"]
        results = [event for event in session if event.kind == "result"]

        if len(hits) != 1 or len(seconds) != 1 or len(thirds) != 1 or len(destroys) != 1 or len(results) != 1:
            check(f"session.{index}.events", False, "missing key event")
            continue

        first_id = hits[0].fields[2]
        second_id = seconds[0].fields[0]
        third_id = thirds[0].fields[0]

        check(
            f"session.{index}.ids",
            first_id == destroys[0].fields[0]
            and len({first_id, second_id, third_id}) == 3,
            f"first={first_id} second={second_id} third={third_id} destroy={destroys[0].fields[0]}",
        )
        check(
            f"session.{index}.second.ode",
            seconds[0].fields[1] == "1",
            f"physx_instance={seconds[0].fields[1]}",
        )
        check(
            f"session.{index}.third.reuse",
            thirds[0].fields[1] == "0",
            f"physx_instance_before={thirds[0].fields[1]}",
        )
        check(
            f"session.{index}.result",
            results[0].fields == ("1", "1", "1", "0", "1"),
            f"values={results[0].fields}",
        )

    reloads = [event for event in events if event.kind == "reload"]
    reload_numbers = [int(event.fields[0]) for event in reloads]
    reloading = [event for event in events if event.kind == "reloading"]
    check("reload.sequence", reload_numbers == [1, 2], f"numbers={reload_numbers}")
    check("reload.levels", len(reloading) == 2, f"count={len(reloading)}")
    check("reload.cap", all(number <= 2 for number in reload_numbers), f"numbers={reload_numbers}")

    tick1 = [event for event in events if event.kind == "tick1"]
    check("reset.tick1", len(tick1) == 3, f"count={len(tick1)}")

    stamps = [event.stamp for event in events]
    check("timestamp.monotonic", stamps == sorted(stamps), "key events are ordered")

    found_markers = [marker for marker in CRASH_MARKERS if marker in text]
    check("crash.markers", not found_markers, f"found={found_markers}")

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s): {', '.join(failures)}")
        return 1
    print("PASS: strict multi-NPC + reload contract satisfied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
