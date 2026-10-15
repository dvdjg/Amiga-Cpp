# VALIDATION — Demo 105 (`tile_scroll_xyunlimited_dualpf`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA (cadencia alterna)** — deuda abajo.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| cadencia (6 frames consecutivos, settle 2 s) | `run-demo.sh … --settle-ms 2000 --sequence-step-frames 6` + `out/tmp/cadence.mjs 0 64 320 128 frame_*.png` | pares: **46,1 \| 0,0 \| 46,2 \| 0,0 \| 46,1** (repetidos=2) | patrón **alterno**: la banda cambia en un par y no en el siguiente |

## Conclusiones

1. Alternancia exacta 46/0: el contenido visible avanza en frames alternos (¿scroll a 2 frames
   por paso? ¿doble buffer que se captura dos veces por actualización? ¿artefacto de captura
   periódico?). Distinto del artefacto puntual de 101/104: aquí es periódico y limpio.
2. Pendiente: fps, visión y el nº de frame de la secuencia para confirmar la periodicidad.

## Deuda registrada

Cadencia de 105 (alterna 46/0 en 6 frames).
