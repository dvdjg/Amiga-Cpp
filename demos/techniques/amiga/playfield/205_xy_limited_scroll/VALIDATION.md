# VALIDATION — Demo 205 (`xy_limited_scroll`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: VERIFICADA (cadencia uniforme)** — visión pendiente.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| cadencia (6 frames consecutivos, settle 2 s) | `run-demo.sh … --settle-ms 2000 --sequence-step-frames 6` + `out/tmp/cadence.mjs 0 64 320 128 frame_*.png` | pares: **18,5 \| 18,6 \| 18,7 \| 18,7 \| 18,7** | cadencia **uniforme** (variación < 1 %) |

## Conclusiones

1. Cadencia uniforme ✓ en la banda medida.
2. Pendiente: fps y visión de secuencia (no bloqueante).

## Deuda registrada

Fps/visión de 205 (pendiente, no bloqueante).
