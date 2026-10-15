# VALIDATION — Demo 103 (`tile_scroll_ring`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA (cadencia irregular)** — deuda abajo.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| cadencia (6 frames consecutivos, settle 2 s) | `run-demo.sh … --settle-ms 2000 --sequence-step-frames 6` + `out/tmp/cadence.mjs 0 64 320 128 frame_*.png` | pares: **86,5 \| 0,0 \| 29,1 \| 49,1 \| 0,0** (repetidos=2) | irregular: dos frames sin cambio y saltos de tamaños distintos |

## Conclusiones

1. Patrón irregular (no es el artefacto único de cruce de palabra de 101/104): dos 0,0 % y saltos
   dispares. Compatible con la ruta de la cámara (fases con paradas) **o** con el artefacto de
   captura repetido; hace falta el nº de frame de la secuencia y una ventana más larga para
   separar coreografía de defecto.
2. Pendiente: fps y visión.

## Deuda registrada

Cadencia de 103 (irregular en 6 frames; separar coreografía vs artefacto con ventana mayor).
