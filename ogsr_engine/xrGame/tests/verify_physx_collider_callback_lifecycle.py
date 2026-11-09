#!/usr/bin/env python3
"""Regression check for the G5.0-I1 legacy/v2 collider callback split.

The engine must not register the v2 collider debug callback for a legacy v1
targeted ragdoll. A v2 run must still register, draw and unregister it.

This is a log-level runtime regression test. It is intentionally independent
of the game process so it can run in CI with captured logs.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--v1-log", required=True, type=Path)
    parser.add_argument("--v2-log", required=True, type=Path)
    parser.add_argument("--mixed-log", type=Path)
    args = parser.parse_args()

    failures: list[str] = []

    v1 = read(args.v1_log)
    require("PhysX ragdoll created" in v1, "v1 log has no ragdoll creation", failures)
    require(
        "collider debug render registered" not in v1,
        "v1 legacy activation registered the v2 collider debug callback",
        failures,
    )
    require(
        "collider debug draw active" not in v1,
        "v1 legacy activation drew v2 collider debug state",
        failures,
    )

    v2 = read(args.v2_log)
    require("PhysX ragdoll created" in v2, "v2 log has no ragdoll creation", failures)
    require(
        "collider debug render registered" in v2,
        "v2 activation did not register the collider debug callback",
        failures,
    )
    require(
        "collider debug draw active shapes=21 joints=10" in v2,
        "v2 activation did not draw the expected 21 shapes / 10 joints",
        failures,
    )
    require(
        "collider debug render unregistered" in v2,
        "v2 teardown did not unregister the collider debug callback",
        failures,
    )

    if args.mixed_log:
        mixed = read(args.mixed_log)
        require(
            "collider debug render registered" in mixed,
            "mixed log has no v2 registration",
            failures,
        )
        require(
            "collider debug render unregistered" in mixed,
            "mixed log has no final v2 unregistration",
            failures,
        )
        # A v1 release must not remove the callback while a v2 state is alive.
        require(
            mixed.find("collider debug render unregistered")
            > mixed.find("collider debug render registered"),
            "mixed log unregistered before registering",
            failures,
        )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print(
        "PASS: v1 did not register; v2 registered, drew and unregistered"
        + ("; mixed log ordering valid" if args.mixed_log else "")
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
