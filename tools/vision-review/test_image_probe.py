#!/usr/bin/env python3
"""Unit tests for image_probe.py (run with ``python -m unittest``)."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from image_probe import (  # noqa: E402
    BBox,
    crop,
    load_rgb,
    parse_box,
    parse_color,
    probe_color,
    scale_nearest,
    save_png,
)
from PIL import Image, ImageDraw  # noqa: E402


class ImageProbeTests(unittest.TestCase):
    def test_parse_color(self) -> None:
        self.assertEqual(parse_color("white"), (255, 255, 255))
        self.assertEqual(parse_color("#0f0"), (0, 255, 0))
        self.assertEqual(parse_color("#102030"), (16, 32, 48))
        self.assertEqual(parse_color("1, 2, 3"), (1, 2, 3))

    def test_parse_box(self) -> None:
        self.assertEqual(parse_box("1,2,3,4"), (1, 2, 3, 4))

    def test_probe_bbox_and_bands(self) -> None:
        image = Image.new("RGB", (20, 12), (0, 0, 0))
        draw = ImageDraw.Draw(image)
        draw.rectangle((3, 2, 7, 4), fill=(255, 255, 255))
        draw.rectangle((3, 6, 7, 8), fill=(255, 255, 255))

        result = probe_color(image, (255, 255, 255))
        self.assertEqual(result.pixels, 30)
        self.assertEqual(result.bbox, BBox(3, 2, 8, 9))
        self.assertEqual(result.rows, tuple((y, 5) for y in range(2, 5)) +
                                      tuple((y, 5) for y in range(6, 9)))
        self.assertEqual(result.bands, (BBox(3, 2, 8, 5), BBox(3, 6, 8, 9)))

        region = probe_color(image, (255, 255, 255), region=(0, 5, 10, 12))
        self.assertEqual(region.bbox, BBox(3, 6, 8, 9))
        self.assertEqual(region.bands, (BBox(3, 6, 8, 9),))

    def test_probe_tolerance_and_min_count(self) -> None:
        image = Image.new("RGB", (4, 1), (0, 0, 0))
        image.putpixel((0, 0), (250, 250, 250))
        image.putpixel((1, 0), (255, 255, 255))
        self.assertEqual(probe_color(image, (255, 255, 255), tolerance=0).pixels, 1)
        self.assertEqual(probe_color(image, (255, 255, 255), tolerance=8).pixels, 2)
        self.assertEqual(probe_color(image, (255, 255, 255), min_count=2).bands, ())

    def test_crop_clamps_and_saves(self) -> None:
        image = Image.new("RGB", (8, 8), (0, 0, 0))
        self.assertEqual(crop(image, (-5, -5, 3, 3)).size, (3, 3))
        self.assertEqual(crop(image, (8, 8, 9, 9)).size, (1, 1))
        out = Path(__file__).resolve().parent / "_tmp_image_probe.png"
        try:
            save_png(scale_nearest(image, 2), out)
            self.assertEqual(load_rgb(out).size, (16, 16))
        finally:
            out.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
