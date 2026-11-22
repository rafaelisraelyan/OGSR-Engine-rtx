#!/usr/bin/env python3
"""Verify R02 Float32 field metadata/checksums and finite values."""

import array
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from r02_diagnostics import read_float32_field, validate_field_metadata

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "artifacts" / "v23h_a03b_r02" / "final"


def main():
    bins = sorted(EVIDENCE.rglob("*.bin"))
    errors = []
    finite = 0
    nonfinite = 0
    for path in bins:
        meta_path = path.with_suffix(".json")
        if not meta_path.exists():
            errors.append(f"missing metadata: {path}")
            continue
        with open(meta_path, encoding="utf-8") as handle:
            meta = json.load(handle)
        try:
            validate_field_metadata(meta)
            values = read_float32_field(path, meta)
        except Exception as exc:  # noqa: BLE001 - evidence verifier
            errors.append(f"{path}: {exc}")
            continue
        for value in values:
            if math.isfinite(value):
                finite += 1
            else:
                nonfinite += 1
    print(f"fields={len(bins)} finite_values={finite} nonfinite_values={nonfinite} errors={len(errors)}")
    for error in errors:
        print("ERROR", error)
    return 1 if errors or nonfinite else 0


if __name__ == "__main__":
    sys.exit(main())
