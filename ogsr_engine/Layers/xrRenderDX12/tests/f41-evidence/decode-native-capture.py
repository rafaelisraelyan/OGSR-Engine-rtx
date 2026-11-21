import argparse
import hashlib
import json
import re
import struct
import zlib
from pathlib import Path

CAPTURE = re.compile(
    r"ph_dx12_capture: saved frames=(\d+) first_present=(\d+) last_present=(\d+)"
    r" fence=(\d+) width=(\d+) height=(\d+) pitch=(\d+) stride=(\d+)"
)
FRAME = re.compile(
    r"ph_dx12_capture: sequence=(\d+) present=(\d+) scene_frame=(\d+) fence=(\d+) slot=(\d+)"
)
METRICS = re.compile(r"ph_dx12_metrics: frame=(\d+) present=(\d+) (.+)$")


def write_png(path, width, height, pixels, pitch):
    row = width * 4
    scanlines = b"".join(b"\0" + pixels[y * pitch : y * pitch + row] for y in range(height))

    def chunk(kind, data):
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", zlib.crc32(kind + data))
        )

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    content = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(scanlines, 5))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(content)
    return hashlib.sha256(content).hexdigest()


def decode(run):
    log = max((run / "appdata/logs").glob("*.log"), key=lambda path: path.stat().st_mtime)
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    matches = [CAPTURE.search(line) for line in lines]
    completed = [match for match in matches if match]
    if len(completed) != 1:
        raise ValueError(f"Expected one complete capture, found {len(completed)}")

    count, first, last, last_fence, width, height, pitch, stride = map(int, completed[0].groups())
    if count != 30 or last - first + 1 != count or pitch < width * 4:
        raise ValueError("Invalid capture layout or Present sequence")
    records = {}
    metrics = {}
    for line in lines:
        match = FRAME.search(line)
        if match:
            number, present, scene_frame, fence, slot = map(int, match.groups())
            if first <= present <= last:
                records[present] = (number, scene_frame, fence, slot)
        match = METRICS.search(line)
        if match:
            metrics[int(match[2])] = dict(part.split("=", 1) for part in match[3].split())
    if sorted(records) != list(range(first, last + 1)):
        raise ValueError("A record is absent for a successful Present")
    if records[last][2] != last_fence:
        raise ValueError("The completion fence differs from the last captured fence")

    raw = run / "native-frames.rgba"
    if raw.stat().st_size != stride * count:
        raise ValueError("Raw capture length differs from the engine record")
    frames = run / "frames"
    frames.mkdir(exist_ok=True)
    manifest = []
    with raw.open("rb") as data:
        for index, present in enumerate(range(first, last + 1)):
            number, scene_frame, fence, slot = records[present]
            if number != index + 1:
                raise ValueError("Capture sequence is out of order")
            pixels = data.read(stride)
            path = frames / f"present-{present:06d}.png"
            sha256 = write_png(path, width, height, pixels, pitch)
            row = dict(
                image=path.name, sha256=sha256, present=present, scene_frame=scene_frame,
                fence=fence, slot=slot, width=width, height=height, metrics=metrics.get(present, {}),
            )
            manifest.append(row)
    (frames / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"{len(manifest)} PNGs written with native Present identities and SHA-256 digests")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("run", type=Path)
    decode(parser.parse_args().run)
