#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Reusable image probing helpers for vision/debug evidence.

This module replaces ad-hoc inline Pillow snippets used to answer questions like
"where is the HUD text?", "which rows contain white ink?" or "is this capture
empty?". It has no dependency beyond Pillow and is deliberately small.

Coordinates use Pillow conventions: boxes are ``(left, top, right, bottom)`` with
``right``/``bottom`` exclusive. Colors are RGB tuples. A region is clamped to the
image; an empty region yields no pixels instead of an exception.

CLI examples
------------
    python tools/vision-review/image_probe.py probe shot.png --color white \
        --region 0,0,1200,300 --min-count 3
    python tools/vision-review/image_probe.py crop shot.png hud.png \
        --box 0,0,700,120
    python tools/vision-review/image_probe.py scale hud.png hud4x.png --factor 4

Library examples
----------------
    from image_probe import load_rgb, probe_color, crop, save_png
    image = load_rgb("out/vision/demo.png")
    white = probe_color(image, (255, 255, 255), region=(0, 0, 1200, 300))
    if white.bbox:
        save_png(crop(image, white.bbox.as_tuple()), "out/vision/hud.png")

The deterministic probe is the primary evidence. A vision model can confirm what
the pixels mean, but must not be the only source for "text exists at X, Y".
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, Sequence

from PIL import Image

Color = tuple[int, int, int]
Box = tuple[int, int, int, int]


@dataclass(frozen=True)
class BBox:
    """Inclusive top/left, exclusive right/bottom."""

    left: int
    top: int
    right: int
    bottom: int

    @property
    def width(self) -> int:
        return self.right - self.left

    @property
    def height(self) -> int:
        return self.bottom - self.top

    def as_tuple(self) -> Box:
        return self.left, self.top, self.right, self.bottom

    def as_dict(self) -> dict[str, int]:
        return {
            "left": self.left,
            "top": self.top,
            "right": self.right,
            "bottom": self.bottom,
            "width": self.width,
            "height": self.height,
        }


@dataclass(frozen=True)
class ProbeResult:
    """Result of a color probe over an optional region."""

    bbox: BBox | None
    pixels: int
    rows: tuple[tuple[int, int], ...]
    bands: tuple[BBox, ...]


def parse_color(value: str) -> Color:
    """Parse ``white``, ``black``, ``#rgb``, ``#rrggbb`` or ``r,g,b``."""
    named = {
        "white": (255, 255, 255),
        "black": (0, 0, 0),
        "red": (255, 0, 0),
        "green": (0, 255, 0),
        "blue": (0, 0, 255),
    }
    text = value.strip().lower()
    if text in named:
        return named[text]
    if text.startswith("#"):
        text = text[1:]
        if len(text) == 3:
            return tuple(int(ch * 2, 16) for ch in text)  # type: ignore[return-value]
        if len(text) == 6:
            return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]
    parts = re.split(r"[\s,;]+", text)
    if len(parts) == 3:
        rgb = tuple(int(p, 10) for p in parts)
        if all(0 <= c <= 255 for c in rgb):
            return rgb  # type: ignore[return-value]
    raise ValueError(f"color no valido: {value!r} (usa white/#rrggbb/r,g,b)")


def parse_box(value: str) -> Box:
    """Parse ``left,top,right,bottom`` (Pillow convention)."""
    parts = re.split(r"[\s,;]+", value.strip())
    if len(parts) != 4:
        raise ValueError(f"box no valido: {value!r} (usa left,top,right,bottom)")
    left, top, right, bottom = (int(p, 10) for p in parts)
    return left, top, right, bottom


def load_rgb(path: str | Path) -> Image.Image:
    """Load an image as RGB; raises ``FileNotFoundError``/``OSError`` clearly."""
    with Image.open(path) as image:
        return image.convert("RGB")


def save_png(image: Image.Image, path: str | Path) -> Path:
    """Save an image as PNG, creating parent directories as needed."""
    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    image.save(out, format="PNG")
    return out


def crop(image: Image.Image, box: Box | BBox) -> Image.Image:
    """Crop using a clamped box; an empty result is a 1x1 black image."""
    left, top, right, bottom = _clamp_box(image, box)
    if right <= left or bottom <= top:
        return Image.new("RGB", (1, 1))
    return image.crop((left, top, right, bottom))


def scale_nearest(image: Image.Image, factor: int) -> Image.Image:
    """Upscale/downscale with nearest-neighbor (keeps pixel edges for inspection)."""
    if factor <= 0:
        raise ValueError("factor debe ser > 0")
    return image.resize(
        (image.width * factor, image.height * factor),
        Image.Resampling.NEAREST,
    )


def _clamp_box(image: Image.Image, box: Box | BBox) -> Box:
    if isinstance(box, BBox):
        left, top, right, bottom = box.as_tuple()
    else:
        left, top, right, bottom = box
    left = max(0, min(image.width, left))
    top = max(0, min(image.height, top))
    right = max(0, min(image.width, right))
    bottom = max(0, min(image.height, bottom))
    return left, top, right, bottom


def _matches(pixel: tuple[int, int, int], color: Color, tolerance: int) -> bool:
    return (
        abs(pixel[0] - color[0]) <= tolerance
        and abs(pixel[1] - color[1]) <= tolerance
        and abs(pixel[2] - color[2]) <= tolerance
    )


def iter_color_pixels(
    image: Image.Image,
    color: Color = (255, 255, 255),
    tolerance: int = 0,
    region: Box | BBox | None = None,
) -> Iterator[tuple[int, int]]:
    """Yield absolute ``(x, y)`` coordinates matching ``color`` within tolerance."""
    if tolerance < 0:
        raise ValueError("tolerance debe ser >= 0")
    left, top, right, bottom = _clamp_box(image, region or (0, 0, image.width, image.height))
    pixels = image.load()
    for y in range(top, bottom):
        for x in range(left, right):
            if _matches(pixels[x, y], color, tolerance):
                yield x, y


def probe_color(
    image: Image.Image,
    color: Color = (255, 255, 255),
    tolerance: int = 0,
    region: Box | BBox | None = None,
    min_count: int = 1,
) -> ProbeResult:
    """Return bbox, row histogram and contiguous row bands for a color.

    ``min_count`` filters rows with very few matches (anti-aliased noise). A band
    is a contiguous set of rows; its bbox spans the pixels of those rows.
    """
    if min_count < 1:
        raise ValueError("min_count debe ser >= 1")
    left, top, right, bottom = _clamp_box(image, region or (0, 0, image.width, image.height))
    row_counts: dict[int, int] = {}
    min_x = image.width
    min_y = image.height
    max_x = -1
    max_y = -1
    count = 0
    for x, y in iter_color_pixels(image, color, tolerance, (left, top, right, bottom)):
        count += 1
        row_counts[y] = row_counts.get(y, 0) + 1
        min_x = min(min_x, x)
        max_x = max(max_x, x)
        min_y = min(min_y, y)
        max_y = max(max_y, y)

    rows = tuple(sorted(row_counts.items()))
    active = [y for y, n in rows if n >= min_count]
    bands: list[BBox] = []
    for start, end in _runs(active):
        band_min_x, band_max_x = image.width, -1
        for y in range(start, end + 1):
            for x in range(left, right):
                if _matches(image.getpixel((x, y)), color, tolerance):
                    band_min_x = min(band_min_x, x)
                    band_max_x = max(band_max_x, x)
        if band_max_x >= 0:
            bands.append(BBox(band_min_x, start, band_max_x + 1, end + 1))

    bbox = None
    if count:
        bbox = BBox(min_x, min_y, max_x + 1, max_y + 1)
    return ProbeResult(bbox=bbox, pixels=count, rows=rows, bands=tuple(bands))


def _runs(values: Iterable[int]) -> list[tuple[int, int]]:
    """Group sorted integers into inclusive contiguous runs."""
    result: list[tuple[int, int]] = []
    for value in values:
        if not result or value != result[-1][1] + 1:
            result.append((value, value))
        else:
            result[-1] = (result[-1][0], value)
    return result


def probe_row_bands(
    image: Image.Image,
    color: Color = (255, 255, 255),
    tolerance: int = 0,
    region: Box | BBox | None = None,
    min_count: int = 1,
) -> tuple[BBox, ...]:
    """Convenience wrapper returning only the text-like row bands."""
    return probe_color(image, color, tolerance, region, min_count).bands


def _format_probe(result: ProbeResult, as_json: bool) -> str:
    if as_json:
        return json.dumps(
            {
                "bbox": result.bbox.as_dict() if result.bbox else None,
                "pixels": result.pixels,
                "bands": [band.as_dict() for band in result.bands],
                "rows": [{"y": y, "pixels": n} for y, n in result.rows],
            },
            indent=2,
        )
    lines = [
        f"pixels={result.pixels}",
        f"bbox={result.bbox.as_dict() if result.bbox else None}",
        f"bands={[band.as_dict() for band in result.bands]}",
        "rows=" + ", ".join(f"{y}:{n}" for y, n in result.rows),
    ]
    return "\n".join(lines)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Image probe/crop/scale helper")
    sub = parser.add_subparsers(dest="command", required=True)

    probe = sub.add_parser("probe", help="busca un color y describe bbox/bandas")
    probe.add_argument("image")
    probe.add_argument("--color", default="white", help="white/#rrggbb/r,g,b")
    probe.add_argument("--tolerance", type=int, default=0)
    probe.add_argument("--region", default=None, help="left,top,right,bottom")
    probe.add_argument("--min-count", type=int, default=1)
    probe.add_argument("--json", action="store_true")

    crop_p = sub.add_parser("crop", help="recorta una imagen y la guarda en PNG")
    crop_p.add_argument("image")
    crop_p.add_argument("output")
    crop_p.add_argument("--box", required=True, help="left,top,right,bottom")

    scale_p = sub.add_parser("scale", help="escala nearest-neighbor y guarda PNG")
    scale_p.add_argument("image")
    scale_p.add_argument("output")
    scale_p.add_argument("--factor", type=int, required=True)

    args = parser.parse_args(argv)
    try:
        if args.command == "probe":
            image = load_rgb(args.image)
            result = probe_color(
                image,
                parse_color(args.color),
                args.tolerance,
                parse_box(args.region) if args.region else None,
                args.min_count,
            )
            print(_format_probe(result, args.json))
            return 0
        if args.command == "crop":
            image = load_rgb(args.image)
            print(save_png(crop(image, parse_box(args.box)), args.output))
            return 0
        if args.command == "scale":
            image = load_rgb(args.image)
            print(save_png(scale_nearest(image, args.factor), args.output))
            return 0
    except (OSError, ValueError) as exc:
        print(f"image_probe: {exc}", file=sys.stderr)
        return 2
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
