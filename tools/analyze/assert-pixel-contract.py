#!/usr/bin/env python3

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple

try:
    from PIL import Image
except Exception as exc:  # pragma: no cover
    raise SystemExit(f"Pillow no disponible: {exc}")


@dataclass
class Viewport:
    x: int
    y: int
    w: int
    h: int


@dataclass
class CoordinateSpace:
    logical_w: int
    logical_h: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Valida una secuencia con contrato de pixeles.")
    parser.add_argument("--sequence-dir", required=True, help="Carpeta con frame_*.png")
    parser.add_argument("--contract", required=True, help="JSON con reglas de pixel")
    parser.add_argument("--run-report", default="", help="run-report.json opcional")
    parser.add_argument("--out-dir", default="", help="Directorio de salida")
    return parser.parse_args()


def load_json(path: Path) -> Dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def list_frames(sequence_dir: Path) -> List[Path]:
    frames = sorted(sequence_dir.glob("frame_*.png"))
    if not frames:
        raise SystemExit(f"No se encontraron frame_*.png en {sequence_dir}")
    return frames


def detect_non_black_viewport(image: Image.Image, threshold: int = 8) -> Viewport:
    rgb = image.convert("RGB")
    pixels = rgb.load()
    width, height = rgb.size
    left = width
    right = -1
    top = height
    bottom = -1

    for y in range(height):
        for x in range(width):
            r, g, b = pixels[x, y]
            if max(r, g, b) > threshold:
                if x < left:
                    left = x
                if x > right:
                    right = x
                if y < top:
                    top = y
                if y > bottom:
                    bottom = y

    if right < left or bottom < top:
        return Viewport(0, 0, width, height)
    return Viewport(left, top, right - left + 1, bottom - top + 1)


def resolve_viewport(contract: Dict, first_frame: Path) -> Viewport:
    viewport = contract.get("viewport", {"mode": "auto_non_black"})
    mode = viewport.get("mode", "auto_non_black")
    image = Image.open(first_frame)
    try:
        if mode == "auto_non_black":
            threshold = int(viewport.get("threshold", 8))
            return detect_non_black_viewport(image, threshold)
        if mode == "fixed":
            return Viewport(
                int(viewport.get("x", 0)),
                int(viewport.get("y", 0)),
                int(viewport.get("w", image.size[0])),
                int(viewport.get("h", image.size[1])),
            )
        raise SystemExit(f"viewport.mode no soportado: {mode}")
    finally:
        image.close()


def resolve_coordinate_space(contract: Dict, viewport: Viewport) -> CoordinateSpace:
    viewport_cfg = contract.get("viewport", {})
    logical_w = int(viewport_cfg.get("logicalWidth", viewport.w))
    logical_h = int(viewport_cfg.get("logicalHeight", viewport.h))
    logical_w = max(1, logical_w)
    logical_h = max(1, logical_h)
    return CoordinateSpace(logical_w=logical_w, logical_h=logical_h)


def load_frame_pixels(path: Path):
    image = Image.open(path).convert("RGB")
    pixels = image.load()
    return image, pixels


def clamp_roi(
    roi: Dict,
    viewport: Viewport,
    frame_size: Tuple[int, int],
    coords: CoordinateSpace,
) -> Tuple[int, int, int, int]:
    frame_w, frame_h = frame_size

    logical_x = int(roi.get("x", 0))
    logical_y = int(roi.get("y", 0))
    logical_w = int(roi.get("w", coords.logical_w))
    logical_h = int(roi.get("h", coords.logical_h))

    scale_x = viewport.w / float(coords.logical_w)
    scale_y = viewport.h / float(coords.logical_h)

    x = viewport.x + int(round(logical_x * scale_x))
    y = viewport.y + int(round(logical_y * scale_y))
    w = max(1, int(round(logical_w * scale_x)))
    h = max(1, int(round(logical_h * scale_y)))

    x0 = max(0, x)
    y0 = max(0, y)
    x1 = min(frame_w, x + w)
    y1 = min(frame_h, y + h)
    if x1 <= x0 or y1 <= y0:
        return 0, 0, 0, 0
    return x0, y0, x1 - x0, y1 - y0


def pixel_diff_exceeds(a: Tuple[int, int, int], b: Tuple[int, int, int], tolerance: int) -> bool:
    return abs(a[0] - b[0]) > tolerance or abs(a[1] - b[1]) > tolerance or abs(a[2] - b[2]) > tolerance


def direction_name(dx: int, dy: int) -> str:
    if dx == 0 and dy == 0:
        return "static"
    horizontal = "right" if dx > 0 else ("left" if dx < 0 else "")
    vertical = "down" if dy > 0 else ("up" if dy < 0 else "")
    if horizontal and vertical:
        return f"{vertical}-{horizontal}"
    if horizontal:
        return horizontal
    return vertical


def direction_compatible(observed: str, expected: str) -> bool:
    if expected == "static":
        return observed == "static"
    parts = expected.split("-")
    return all(part in observed for part in parts)


def shifted_region_error_ratio(
    pixels_a,
    pixels_b,
    frame_size: Tuple[int, int],
    roi_abs: Tuple[int, int, int, int],
    dx: int,
    dy: int,
    tolerance: int,
) -> Dict:
    frame_w, frame_h = frame_size
    x0, y0, w, h = roi_abs
    if w <= 0 or h <= 0:
        return {"compared": 0, "mismatched": 0, "errorRatio": 1.0}

    compared = 0
    mismatched = 0
    for y in range(y0, y0 + h):
        by = y + dy
        if by < 0 or by >= frame_h:
            continue
        for x in range(x0, x0 + w):
            bx = x + dx
            if bx < 0 or bx >= frame_w:
                continue
            compared += 1
            if pixel_diff_exceeds(pixels_a[x, y], pixels_b[bx, by], tolerance):
                mismatched += 1

    if compared == 0:
        return {"compared": 0, "mismatched": 0, "errorRatio": 1.0}
    return {
        "compared": compared,
        "mismatched": mismatched,
        "errorRatio": mismatched / compared,
    }


def best_shift_for_roi(
    pixels_a,
    pixels_b,
    frame_size: Tuple[int, int],
    roi_abs: Tuple[int, int, int, int],
    tolerance: int,
    search_radius: int,
) -> Dict:
    best = {
        "dx": 0,
        "dy": 0,
        "errorRatio": 1.0,
        "compared": 0,
        "mismatched": 0,
    }
    for dy in range(-search_radius, search_radius + 1):
        for dx in range(-search_radius, search_radius + 1):
            result = shifted_region_error_ratio(
                pixels_a,
                pixels_b,
                frame_size,
                roi_abs,
                dx,
                dy,
                tolerance,
            )
            if result["errorRatio"] < best["errorRatio"]:
                best = {
                    "dx": dx,
                    "dy": dy,
                    "errorRatio": result["errorRatio"],
                    "compared": result["compared"],
                    "mismatched": result["mismatched"],
                }
    best["direction"] = direction_name(best["dx"], best["dy"])
    return best


def forbidden_color_ratio(
    pixels,
    frame_size: Tuple[int, int],
    roi_abs: Tuple[int, int, int, int],
    color: Tuple[int, int, int],
    tolerance: int,
) -> Dict:
    x0, y0, w, h = roi_abs
    if w <= 0 or h <= 0:
        return {"total": 0, "matches": 0, "ratio": 1.0}
    total = 0
    matches = 0
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            total += 1
            p = pixels[x, y]
            if (
                abs(p[0] - color[0]) <= tolerance
                and abs(p[1] - color[1]) <= tolerance
                and abs(p[2] - color[2]) <= tolerance
            ):
                matches += 1
    if total == 0:
        return {"total": 0, "matches": 0, "ratio": 1.0}
    return {"total": total, "matches": matches, "ratio": matches / total}


def pair_range(segment: Dict, frame_count: int) -> Tuple[int, int]:
    frames = segment.get("frames")
    if not isinstance(frames, list) or len(frames) != 2:
        return 0, max(0, frame_count - 1)
    start = int(frames[0])
    end = int(frames[1])
    if end == -1:
        end = frame_count - 1
    start = max(0, min(start, frame_count - 1))
    end = max(0, min(end, frame_count - 1))
    if end < start:
        start, end = end, start
    return start, end


def load_telemetry(run_report_path: Path) -> List[Dict]:
    report = load_json(run_report_path)
    sequence = report.get("sequence", {})
    frames = sequence.get("frames", [])
    telemetry = []
    for frame in frames:
        rs = frame.get("runStatus") or frame.get("runStatusBefore")
        if not rs or not rs.get("ok"):
            continue
        detail = int(rs.get("detail", 0))
        telemetry.append(
            {
                "frame": int(rs.get("frame", 0)),
                "cameraX": (detail >> 16) & 0xFF,
                "cameraY": (detail >> 8) & 0xFF,
                "tileJobs": (detail >> 4) & 0x0F,
                "prefetchFlags": detail & 0x0F,
            }
        )
    return telemetry


def run_assertions(
    frames: List[Path],
    contract: Dict,
    viewport: Viewport,
    coords: CoordinateSpace,
    run_report: Optional[Path],
) -> Dict:
    defaults = contract.get("defaults", {})
    default_tolerance = int(defaults.get("rgbTolerance", 8))
    default_error_ratio = float(defaults.get("maxErrorRatio", 0.01))
    frame_images = []
    frame_pixels = []
    for frame in frames:
        image, pixels = load_frame_pixels(frame)
        frame_images.append(image)
        frame_pixels.append(pixels)

    try:
        frame_size = frame_images[0].size
        telemetry = []
        if run_report is not None and run_report.exists():
            telemetry = load_telemetry(run_report)

        checks_report = []
        failures = []

        global_checks = contract.get("globalChecks", [])
        for check in global_checks:
            ctype = check.get("type", "")
            if ctype != "forbidden_color_ratio":
                continue
            color = tuple(check.get("color", [0, 0, 0]))
            tolerance = int(check.get("colorTolerance", 0))
            max_ratio = float(check.get("maxRatio", 0.0))
            roi = check.get("roi", {"x": 0, "y": 0, "w": viewport.w, "h": viewport.h})
            roi_abs = clamp_roi(roi, viewport, frame_size, coords)

            per_frame = []
            max_seen = 0.0
            max_seen_frame = 0
            for index in range(len(frames)):
                result = forbidden_color_ratio(
                    frame_pixels[index],
                    frame_size,
                    roi_abs,
                    color,
                    tolerance,
                )
                ratio = result["ratio"]
                per_frame.append({"frame": index, **result})
                if ratio > max_seen:
                    max_seen = ratio
                    max_seen_frame = index

            passed = max_seen <= max_ratio
            item = {
                "scope": "global",
                "type": ctype,
                "name": check.get("name", ctype),
                "roi": {"x": roi_abs[0], "y": roi_abs[1], "w": roi_abs[2], "h": roi_abs[3]},
                "maxRatio": max_ratio,
                "maxSeenRatio": max_seen,
                "maxSeenFrame": max_seen_frame,
                "passed": passed,
                "details": per_frame,
            }
            checks_report.append(item)
            if not passed:
                failures.append({
                    "name": item["name"],
                    "reason": f"maxSeenRatio={max_seen:.6f} > maxRatio={max_ratio:.6f}",
                    "frame": max_seen_frame,
                })

        for segment in contract.get("segments", []):
            start, end = pair_range(segment, len(frames))
            segment_name = segment.get("name", "segment")
            for check in segment.get("checks", []):
                ctype = check.get("type", "")
                roi = check.get("roi", {"x": 0, "y": 0, "w": viewport.w, "h": viewport.h})
                roi_abs = clamp_roi(roi, viewport, frame_size, coords)
                tolerance = int(check.get("rgbTolerance", default_tolerance))
                max_error_ratio = float(check.get("maxErrorRatio", default_error_ratio))

                pair_results = []
                worst_ratio = 0.0
                worst_pair = [start, min(start + 1, len(frames) - 1)]

                for i in range(start, end):
                    if i + 1 >= len(frames):
                        break

                    dx = 0
                    dy = 0
                    if ctype == "shifted_region_match":
                        logical_dx = int(check.get("dx", 0))
                        logical_dy = int(check.get("dy", 0))
                        dx = int(round(logical_dx * (viewport.w / float(coords.logical_w))))
                        dy = int(round(logical_dy * (viewport.h / float(coords.logical_h))))
                    elif ctype == "equal_region":
                        dx = 0
                        dy = 0
                    elif ctype == "telemetry_shift_match":
                        factor_x = int(check.get("cameraXToContentDx", 0))
                        factor_y = int(check.get("cameraYToContentDy", 0))
                        if i + 1 < len(telemetry):
                            cam_dx = telemetry[i + 1]["cameraX"] - telemetry[i]["cameraX"]
                            cam_dy = telemetry[i + 1]["cameraY"] - telemetry[i]["cameraY"]
                            logical_dx = cam_dx * factor_x
                            logical_dy = cam_dy * factor_y
                            dx = int(round(logical_dx * (viewport.w / float(coords.logical_w))))
                            dy = int(round(logical_dy * (viewport.h / float(coords.logical_h))))
                        else:
                            dx = 0
                            dy = 0
                    elif ctype == "telemetry_direction_match":
                        factor_x = int(check.get("cameraXToContentDx", 0))
                        factor_y = int(check.get("cameraYToContentDy", 0))
                        expected = "static"
                        if i + 1 < len(telemetry):
                            cam_dx = telemetry[i + 1]["cameraX"] - telemetry[i]["cameraX"]
                            cam_dy = telemetry[i + 1]["cameraY"] - telemetry[i]["cameraY"]
                            logical_dx = cam_dx * factor_x
                            logical_dy = cam_dy * factor_y
                            dx = int(round(logical_dx * (viewport.w / float(coords.logical_w))))
                            dy = int(round(logical_dy * (viewport.h / float(coords.logical_h))))
                            expected = direction_name(dx, dy)
                        search_radius = int(check.get("searchRadius", 8))
                        best = best_shift_for_roi(
                            frame_pixels[i],
                            frame_pixels[i + 1],
                            frame_size,
                            roi_abs,
                            tolerance,
                            search_radius,
                        )
                        compatible = direction_compatible(best["direction"], expected)
                        pair_item = {
                            "from": i,
                            "to": i + 1,
                            "expectedDirection": expected,
                            "observedDirection": best["direction"],
                            "observedDx": best["dx"],
                            "observedDy": best["dy"],
                            "errorRatio": best["errorRatio"],
                            "compared": best["compared"],
                            "mismatched": best["mismatched"],
                            "compatible": compatible,
                        }
                        pair_results.append(pair_item)
                        if best["errorRatio"] > worst_ratio:
                            worst_ratio = best["errorRatio"]
                            worst_pair = [i, i + 1]
                        continue
                    else:
                        continue

                    result = shifted_region_error_ratio(
                        frame_pixels[i],
                        frame_pixels[i + 1],
                        frame_size,
                        roi_abs,
                        dx,
                        dy,
                        tolerance,
                    )
                    error_ratio = result["errorRatio"]
                    pair_item = {
                        "from": i,
                        "to": i + 1,
                        "dx": dx,
                        "dy": dy,
                        "errorRatio": error_ratio,
                        "compared": result["compared"],
                        "mismatched": result["mismatched"],
                    }
                    pair_results.append(pair_item)
                    if error_ratio > worst_ratio:
                        worst_ratio = error_ratio
                        worst_pair = [i, i + 1]

                passed = all(item["errorRatio"] <= max_error_ratio for item in pair_results)
                if ctype == "telemetry_direction_match":
                    min_compatible_ratio = float(check.get("minCompatibleRatio", 0.7))
                    compatible_count = sum(1 for item in pair_results if item.get("compatible", False))
                    pair_count = max(1, len(pair_results))
                    compatible_ratio = compatible_count / pair_count
                    passed = compatible_ratio >= min_compatible_ratio
                entry = {
                    "scope": segment_name,
                    "type": ctype,
                    "name": check.get("name", f"{segment_name}:{ctype}"),
                    "range": [start, end],
                    "roi": {"x": roi_abs[0], "y": roi_abs[1], "w": roi_abs[2], "h": roi_abs[3]},
                    "rgbTolerance": tolerance,
                    "maxErrorRatio": max_error_ratio,
                    "worstErrorRatio": worst_ratio,
                    "worstPair": worst_pair,
                    "passed": passed,
                    "pairs": pair_results,
                }
                if ctype == "telemetry_direction_match":
                    compatible_count = sum(1 for item in pair_results if item.get("compatible", False))
                    pair_count = max(1, len(pair_results))
                    entry["compatiblePairs"] = compatible_count
                    entry["pairCount"] = pair_count
                    entry["compatibleRatio"] = compatible_count / pair_count
                    entry["minCompatibleRatio"] = float(check.get("minCompatibleRatio", 0.7))
                checks_report.append(entry)
                if not passed:
                    if ctype == "telemetry_direction_match":
                        failures.append(
                            {
                                "name": entry["name"],
                                "reason": (
                                    f"compatibleRatio={entry['compatibleRatio']:.6f} "
                                    f"< minCompatibleRatio={entry['minCompatibleRatio']:.6f}"
                                ),
                                "frame": worst_pair[0],
                            }
                        )
                        continue
                    failures.append(
                        {
                            "name": entry["name"],
                            "reason": f"worstErrorRatio={worst_ratio:.6f} > maxErrorRatio={max_error_ratio:.6f}",
                            "frame": worst_pair[0],
                        }
                    )

        status = "ok" if not failures else "failed"
        return {
            "status": status,
            "frames": len(frames),
            "viewport": {"x": viewport.x, "y": viewport.y, "w": viewport.w, "h": viewport.h},
            "contractVersion": contract.get("version", 1),
            "checks": checks_report,
            "failures": failures,
            "telemetryFrames": len(telemetry),
        }
    finally:
        for image in frame_images:
            image.close()


def build_summary(report: Dict, sequence_dir: Path, contract_path: Path) -> str:
    lines = []
    lines.append("# Pixel Assert Summary")
    lines.append("")
    lines.append(f"Status: {report['status']}")
    lines.append(f"Frames: {report['frames']}")
    lines.append(f"SequenceDir: {sequence_dir}")
    lines.append(f"Contract: {contract_path}")
    viewport = report["viewport"]
    lines.append(f"Viewport: x={viewport['x']} y={viewport['y']} w={viewport['w']} h={viewport['h']}")
    lines.append(f"TelemetryFrames: {report.get('telemetryFrames', 0)}")
    lines.append("")

    lines.append("## Checks")
    for check in report["checks"]:
        state = "OK" if check["passed"] else "FAIL"
        lines.append(
            f"- [{state}] {check['name']} ({check['type']}) worst={check.get('worstErrorRatio', check.get('maxSeenRatio', 0.0)):.6f}"
        )

    lines.append("")
    lines.append("## Failures")
    if not report["failures"]:
        lines.append("- none")
    else:
        for failure in report["failures"]:
            lines.append(f"- frame {failure['frame']}: {failure['name']} -> {failure['reason']}")

    lines.append("")
    return "\n".join(lines)


def main() -> None:
    args = parse_args()
    sequence_dir = Path(args.sequence_dir).resolve()
    contract_path = Path(args.contract).resolve()
    run_report_path = Path(args.run_report).resolve() if args.run_report else None

    if not sequence_dir.exists() or not sequence_dir.is_dir():
        raise SystemExit(f"SequenceDir invalido: {sequence_dir}")
    if not contract_path.exists():
        raise SystemExit(f"Contract no encontrado: {contract_path}")

    out_dir = Path(args.out_dir).resolve() if args.out_dir else sequence_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    contract = load_json(contract_path)
    frames = list_frames(sequence_dir)
    viewport = resolve_viewport(contract, frames[0])
    coords = resolve_coordinate_space(contract, viewport)
    report = run_assertions(frames, contract, viewport, coords, run_report_path)

    report_path = out_dir / "pixel-assert-report.json"
    summary_path = out_dir / "pixel-assert-summary.md"
    with report_path.open("w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2)
    summary = build_summary(report, sequence_dir, contract_path)
    summary_path.write_text(summary, encoding="utf-8")

    print(f"Status: {report['status']}")
    print(f"Report: {report_path}")
    print(f"Summary: {summary_path}")

    if report["status"] != "ok":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
