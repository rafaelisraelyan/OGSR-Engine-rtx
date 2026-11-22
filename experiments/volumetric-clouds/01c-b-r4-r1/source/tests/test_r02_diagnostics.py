#!/usr/bin/env python3
"""Unit tests for the R02 diagnostic corrections.

Run from tools/VolumetricCloudsTest:
    python -m unittest discover -s tests -p "test_r02_diagnostics.py"
"""

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

import r02_diagnostics as diag


class PercentileTests(unittest.TestCase):
    def test_ascending_definition(self):
        self.assertAlmostEqual(diag.percentile([1, 2, 3, 4], 50.0), 2.5)
        self.assertAlmostEqual(diag.percentile([1, 2, 3, 4], 90.0), 3.7)
        self.assertAlmostEqual(diag.percentile([1, 2, 3, 4], 0.0), 1.0)
        self.assertAlmostEqual(diag.percentile([1, 2, 3, 4], 100.0), 4.0)

    def test_unsorted_and_duplicates(self):
        self.assertAlmostEqual(diag.percentile([9, 1, 5, 5, 3], 50.0), 5.0)
        self.assertAlmostEqual(diag.percentile([], 50.0), 0.0)

    def test_r01_regression_not_descending_index(self):
        # R01 used a descending list and indexed at 0.9*n, which returned a
        # small tail area. The corrected percentile must be in the large tail.
        areas = [1000, 400, 120, 30, 8, 3]
        self.assertAlmostEqual(diag.percentile(areas, 90.0), 700.0)
        self.assertNotAlmostEqual(diag.percentile(areas, 90.0), 3.0)


class AspectTests(unittest.TestCase):
    def test_xy_16km_by_28km(self):
        width, height = diag.world_aspect_size(16000.0, 2800.0, 800)
        self.assertEqual((width, height), (800, 140))

    def test_yz_16km_by_28km(self):
        width, height = diag.world_aspect_size(16000.0, 2800.0, 1024)
        self.assertEqual((width, height), (1024, 179))

    def test_resample_preserves_rows(self):
        src = bytes([255, 0, 0, 0, 255, 0])  # 2x1
        out = diag.resample_nearest(src, 2, 1, 4, 2)
        self.assertEqual(len(out), 4 * 2 * 3)
        self.assertEqual(out[0:3], bytes([255, 0, 0]))


class ComponentTests(unittest.TestCase):
    def test_2d_two_components(self):
        # 4x3, left vertical pair and right single cell
        nx, nz = 4, 3
        mask = [0] * (nx * nz)
        mask[0] = 1
        mask[nx] = 1
        mask[2 * nx + 3] = 1
        stats = diag.component_stats_2d(mask, nx, nz, 100.0)
        self.assertEqual(stats["dimension"], 2)
        self.assertEqual(stats["count"], 2)
        self.assertEqual(stats["largest_cells"], 2)
        self.assertAlmostEqual(stats["largest_fraction"], 2.0 / 3.0)

    def test_3d_diagonal_is_not_connected(self):
        nx, ny, nz = 3, 3, 3
        mask = [0] * (nx * ny * nz)
        mask[0] = 1
        mask[1 + nx + nx * ny] = 1
        stats = diag.component_stats_3d(mask, nx, ny, nz, 1.0)
        self.assertEqual(stats["dimension"], 3)
        self.assertEqual(stats["count"], 2)
        self.assertEqual(stats["occupied_voxels"], 2)

    def test_3d_face_connected(self):
        nx, ny, nz = 2, 2, 2
        mask = [1, 1, 1, 1, 0, 0, 0, 0]
        stats = diag.component_stats_3d(mask, nx, ny, nz, 1.0)
        self.assertEqual(stats["count"], 1)
        self.assertEqual(stats["largest_voxels"], 4)


class FieldMetadataTests(unittest.TestCase):
    def test_roundtrip_and_checksum(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "field.bin"
            meta = {
                "format": "test",
                "field": "occupancy",
                "seed": 1337,
                "grid": [1, 1, 1],
                "world_origin": [0.0, 0.0, 0.0],
                "cell_size_m": [100.0, 100.0, 100.0],
                "axes": ["x", "y", "z"],
                "index_order": "x fastest, then y, then z",
                "algorithm": "test",
                "parameters": {},
                "source_sha256": "0" * 64,
                "provenance": "unit-test",
            }
            digest = diag.write_float32_field(path, [0.25, 0.5], meta)
            loaded_meta = json.loads(path.with_suffix(".json").read_text(encoding="utf-8"))
            self.assertEqual(loaded_meta["sha256"], digest)
            diag.validate_field_metadata(loaded_meta)
            values = diag.read_float32_field(path, loaded_meta)
            self.assertEqual(list(values), [0.25, 0.5])
            path.write_bytes(path.read_bytes()[:-4] + b"\x00\x00\x80\x3f")
            with self.assertRaises(ValueError):
                diag.read_float32_field(path, loaded_meta)


if __name__ == "__main__":
    unittest.main()
