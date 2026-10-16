# VALIDATION — Demo 203 (`world_tilemap_xlimited`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: VERIFICADA (cadencia uniforme)** — visión pendiente.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| cadencia (6 frames consecutivos, settle 2 s) | `run-demo.sh … --settle-ms 2000 --sequence-step-frames 6` + `out/tmp/cadence.mjs 0 64 320 128 frame_*.png` | pares: **25,4 \| 25,3 \| 25,6 \| 25,4 \| 25,5** | cadencia **uniforme** (variación < 1 %) |

## Conclusiones

1. Cadencia uniforme ✓ en la banda medida (movimiento por frame constante).
2. Pendiente: fps y visión de secuencia (no bloqueante).

## Deuda registrada

Fps/visión de 203 (pendiente, no bloqueante).

### Pixel-assert (paso de frame)

`--pixel-assert` verde: `shift-left-1px-per-frame` dx=−1, 8 frames, 0 pares duplicados, 7 pares
desplazados (MeanDiffAvg 18,2; informe `out/regression/20261016-133928`). Sin cambios de código en
esta pasada: la publicación la conduce el `App` (la demo no instala `COP1LC`).
