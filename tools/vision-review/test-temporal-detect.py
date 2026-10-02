#!/usr/bin/env python3
"""Pruebas deterministas de compensación de movimiento y detección A-B-A."""

import importlib.util
from pathlib import Path

import cv2
import numpy as np


MODULE_PATH = Path(__file__).with_name("temporal-detect.py")
SPEC = importlib.util.spec_from_file_location("temporal_detect", MODULE_PATH)
temporal_detect = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(temporal_detect)
DIFF_SPEC = importlib.util.spec_from_file_location(
    "frame_diff", Path(__file__).with_name("frame-diff.py"))
frame_diff = importlib.util.module_from_spec(DIFF_SPEC)
DIFF_SPEC.loader.exec_module(frame_diff)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def textured_frame(width=320, height=128):
    rng = np.random.default_rng(203)
    gray = rng.integers(0, 256, (height, width), dtype=np.uint8)
    return cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)


def main():
    pan_base = cv2.cvtColor(
        np.random.default_rng(203).integers(0, 256, (128, 320), dtype=np.uint8),
        cv2.COLOR_GRAY2BGR)
    pan = [cv2.warpAffine(pan_base, np.float32([[1, 0, -4 * i], [0, 1, 0]]),
                          (pan_base.shape[1], pan_base.shape[0]), flags=cv2.INTER_NEAREST,
                          borderMode=cv2.BORDER_WRAP) for i in range(8)]
    aligned, meta = temporal_detect.align_global_motion(pan)
    _, pan_anomalies = temporal_detect.compute_suspicion(aligned)
    check(meta["applied"], "la traslación global coherente debe compensarse")
    check(len(pan_anomalies) < 10,
          "el paneo uniforme no debe producir candidatos en gran parte de la imagen")

    flicker = [frame.copy() for frame in pan[:5]]
    flicker[2][40:72, 120:152] = 255 - flicker[2][40:72, 120:152]
    aligned_flicker, _ = temporal_detect.align_global_motion(flicker)
    _, flicker_anomalies = temporal_detect.compute_suspicion(aligned_flicker)
    check(any(item["type"] == "flicker" and item["x"] < 180 and item["y"] < 100
              for item in flicker_anomalies),
          "el parpadeo sintético de un frame debe seguir detectándose tras alinear el paneo")

    dx, dy = 4, 0
    aligned_pair = cv2.warpAffine(
        pan[1], np.float32([[1, 0, dx], [0, 1, dy]]),
        (pan_base.shape[1], pan_base.shape[0]), flags=cv2.INTER_NEAREST,
        borderMode=cv2.BORDER_REPLICATE)
    gray_a = cv2.cvtColor(pan[0], cv2.COLOR_BGR2GRAY)
    gray_b = cv2.cvtColor(aligned_pair, cv2.COLOR_BGR2GRAY)
    check(frame_diff.temporal_low_blocks(
        gray_a, gray_b, 16, ignore_rois=[(-16, -16, 32, 160), (288, -16, 32, 160)]) == 0,
          "el paneo de textura de alta frecuencia debe tener cero cambio residual")
    corrupted = aligned_pair.copy()
    cv2.rectangle(corrupted, (120, 40), (151, 71), (255, 255, 255), -1)
    gray_corrupted = cv2.cvtColor(corrupted, cv2.COLOR_BGR2GRAY)
    check(frame_diff.temporal_low_blocks(gray_a, gray_corrupted, 16) > 0,
          "la corrupción sintética tras compensar el paneo debe superar el gate estructural")

    static = [pan_base.copy() for _ in range(4)]
    aligned_static, static_meta = temporal_detect.align_global_motion(static)
    check(not static_meta["applied"], "una secuencia estática no requiere compensación")
    check(aligned_static is static, "si no se aplica, se conserva la secuencia original")
    print("OK: paneo coherente ignorado; flicker A-B-A detectado; estático sin alineación.")


if __name__ == "__main__":
    main()
