# VALIDATION — Demo 130 (`app_scroll_ladder`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA (cadencia con un evento)** — deuda abajo.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| cadencia (6 frames consecutivos, settle 2 s) | `run-demo.sh … --settle-ms 2000 --sequence-step-frames 6` + `out/tmp/cadence.mjs 0 64 320 128 frame_*.png` | pares: **18,4 \| 18,5 \| 18,6 \| 25,7 \| 0,0** (repetidos=1) | uniforme salvo el último par (0,0 %) y un salto previo (25,7 %) |

## Conclusiones

1. Movimiento uniforme (~18,5 %) en los primeros pares; el evento final (25,7 + 0,0) es la misma
   firma que el artefacto de captura de 101/104 o un cambio de fase de la ruta.
2. Pendiente: fps, visión y ventana mayor.

## Deuda registrada

Cadencia de 130 (evento al final de la ventana).
