#!/usr/bin/env python3
"""A02c optical-depth pipeline validation.

1. Analytic band test: the GPU writes known OD values through the same
   Float32 probe buffer used by the analyzer.
2. sRGB comparison: the BMP capture is sRGB-encoded; the .bin is linear.
   Quantify both the A02b interpretation (byte/255 as mapped OD) and the
   correct decode (sRGB -> linear) against the Float32 source.
"""

import argparse
import array
import json
import math
import os
import struct
import sys
from pathlib import Path

BANDS = (0.0, 0.05, 0.10, 0.25, 0.50, 1.00, 2.00, 5.00)


def load_od(path):
    meta = json.load(open(path.with_name("optical_depth_float32.json"),
                          encoding="utf-8"))
    data = array.array("f")
    data.frombytes(path.read_bytes())
    pixels = int(meta["width"]) * int(meta["height"])
    channels = int(meta.get("channels", 4))
    if channels == 1:
        if len(data) != pixels:
            raise ValueError("size mismatch: %s" % path)
        return meta, data, None
    if len(data) != pixels * 4:
        raise ValueError("size mismatch: %s" % path)
    return meta, data[1::4], data[0::4]


def load_bmp(path):
    raw = path.read_bytes()
    offset = struct.unpack_from("<I", raw, 10)[0]
    width = struct.unpack_from("<i", raw, 18)[0]
    height = struct.unpack_from("<i", raw, 22)[0]
    bpp = struct.unpack_from("<H", raw, 28)[0]
    top_down = height < 0
    height = abs(height)
    stride = width * 4
    rows = []
    for y in range(height):
        row = y if top_down else height - 1 - y
        base = offset + row * stride
        rows.append([raw[base + x * 4 + 2] / 255.0 for x in range(width)])
    return width, height, rows


def srgb_to_linear(value):
    if value <= 0.04045:
        return value / 12.92
    return ((value + 0.055) / 1.055) ** 2.4


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pattern", type=Path, required=True)
    parser.add_argument("--srgb-root", type=Path, default=None)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    result = {}
    meta, od_data, mapped_data = load_od(
        args.pattern / "optical_depth_float32.bin")
    width, height = meta["width"], meta["height"]
    worst = 0.0
    mapped_worst = 0.0
    bands_ok = 0
    bands_bad = 0
    for y in range(height):
        od = BANDS[min(y * 8 // height, 7)]
        mapped = od / (od + 1.0)
        i = y * width + width // 2
        error = abs(od_data[i] - od)
        if mapped_data is not None:
            mapped_worst = max(mapped_worst, abs(mapped_data[i] - mapped))
            error = max(error, abs(mapped_data[i] - mapped))
        worst = max(worst, error)
        if error < 1e-6:
            bands_ok += 1
        else:
            bands_bad += 1
    inverse_worst = 0.0
    for od in BANDS:
        mapped = od / (od + 1.0)
        back = mapped / (1.0 - mapped) if mapped < 1.0 else float("inf")
        inverse_worst = max(inverse_worst, abs(back - od))
    result["pattern"] = {
        "width": width,
        "height": height,
        "bands_ok": bands_ok,
        "bands_bad": bands_bad,
        "worst_abs_error": worst,
        "inverse_worst_error": inverse_worst,
        "finite": meta["finite"],
        "pass": bands_bad == 0 and worst < 1e-6 and inverse_worst < 1e-4,
    }

    if args.srgb_root is not None:
        rows = []
        for directory in sorted(args.srgb_root.iterdir()):
            if not directory.is_dir():
                continue
            bin_path = directory / "optical_depth_float32.bin"
            bmp_path = directory / "clouds_sunset_horizon.bmp"
            if not bin_path.is_file() or not bmp_path.is_file():
                continue
            meta, od_data, mapped_data = load_od(bin_path)
            w, h = meta["width"], meta["height"]
            bw, bh, pixels = load_bmp(bmp_path)
            if bw != w * 2 or bh != h * 2:
                continue
            encoded_err = []
            linear_err = []
            for y in range(h):
                for x in range(w):
                    # The composite upsamples the 640x360 cloud texture to
                    # 1280x720; the 2x2 block centre equals the cloud pixel.
                    value = (pixels[y * 2][x * 2] + pixels[y * 2][x * 2 + 1] +
                             pixels[y * 2 + 1][x * 2] +
                             pixels[y * 2 + 1][x * 2 + 1]) / 4.0
                    index = y * w + x
                    mapped = (mapped_data[index] if mapped_data is not None
                              else od_data[index] / (od_data[index] + 1.0))
                    encoded_err.append(abs(value - mapped))
                    linear_err.append(abs(srgb_to_linear(value) - mapped))
            rows.append({
                "station": directory.name,
                "width": w,
                "height": h,
                "mean_abs_encoded_as_linear": sum(encoded_err) / len(encoded_err),
                "max_abs_encoded_as_linear": max(encoded_err),
                "mean_abs_srgb_decoded": sum(linear_err) / len(linear_err),
                "max_abs_srgb_decoded": max(linear_err),
            })
        result["srgb"] = rows
        result["srgb_summary"] = {
            "stations": len(rows),
            "mean_encoded_as_linear": (sum(r["mean_abs_encoded_as_linear"] for r in rows) / len(rows)) if rows else 0.0,
            "mean_srgb_decoded": (sum(r["mean_abs_srgb_decoded"] for r in rows) / len(rows)) if rows else 0.0,
        }

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8") as handle:
        json.dump(result, handle, indent=2)
    print(json.dumps(result.get("pattern"), indent=2))
    if "srgb_summary" in result:
        print(json.dumps(result["srgb_summary"], indent=2))
    return 0 if result["pattern"]["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
