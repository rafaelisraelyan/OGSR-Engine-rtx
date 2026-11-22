#!/usr/bin/env python3
"""Convert the A02c 4-channel OD probe files to one raw-OD float per pixel.

The GPU writes (mapped_od, od, 0, 1) per pixel. The mapped value is a display
transform; the physical quantity is od = -log(T). Packaging one float per pixel
keeps the evidence small and unambiguous. The conversion is lossless for the
od channel; the 4-channel test-pattern capture is kept separately to validate
the GPU path.
"""

import argparse
import array
import json
import os
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    count = 0
    total = 0
    for meta_path in sorted(args.root.rglob("optical_depth_float32.json")):
        directory = meta_path.parent
        meta = json.load(open(meta_path, encoding="utf-8"))
        width = int(meta["width"])
        height = int(meta["height"])
        pixels = width * height
        channels = int(meta.get("channels", 4))
        data = array.array("f")
        data.frombytes((directory / "optical_depth_float32.bin").read_bytes())
        if channels == 4:
            if len(data) != pixels * 4:
                raise ValueError("size mismatch: %s" % directory)
            od = data[1::4]
        elif channels == 1:
            if len(data) != pixels:
                raise ValueError("size mismatch: %s" % directory)
            od = data
        else:
            raise ValueError("unsupported channels: %s" % directory)

        out_dir = args.out / directory.name
        out_dir.mkdir(parents=True, exist_ok=True)
        (out_dir / "optical_depth_float32.bin").write_bytes(od.tobytes())
        out_meta = dict(meta)
        out_meta["channels"] = 1
        out_meta["record_stride_bytes"] = 4
        out_meta["channel_order"] = ["od"]
        out_meta["channel_definition"] = (
            "cloud-only optical depth od = -log(max(T, 1e-6)), before "
            "atmosphere/exposure/tone mapping/sRGB")
        out_meta["mapped_od_relation"] = "mapped_od = od / (od + 1)"
        out_meta["converted_from_channels"] = channels
        out_meta["conversion"] = "pack_a02c_od.py (raw od channel, lossless)"
        with open(out_dir / "optical_depth_float32.json", "w",
                  encoding="utf-8") as handle:
            json.dump(out_meta, handle, indent=2)
        count += 1
        total += os.path.getsize(out_dir / "optical_depth_float32.bin")

    summary = {
        "stations": count,
        "od_bytes": total,
        "format": "float32 little-endian, width*height raw OD",
        "converter": "pack_a02c_od.py",
    }
    args.out.mkdir(parents=True, exist_ok=True)
    with open(args.out / "conversion_summary.json", "w",
              encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2)
    print("stations=%d bytes=%d out=%s" % (count, total, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
