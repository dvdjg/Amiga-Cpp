# VALIDATION — Demo 131 (`app_split_screen`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA (movimiento muy leve en la banda medida)** — deuda abajo.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| cadencia (6 frames consecutivos, settle 2 s) | `run-demo.sh … --settle-ms 2000 --sequence-step-frames 6` + `out/tmp/cadence.mjs 0 64 320 128 frame_*.png` | pares: **0,7 \| 2,4 \| 0,7 \| 0,7 \| 0,7** (repetidos=0) | movimiento pequeño y mayormente uniforme en la banda central |

## Conclusiones

1. La banda medida (y=64..191) cambia poco (0,7 %): el contenido de esa franja es casi estático o
   el scroll es muy lento; el 2,4 % intermedio = un evento puntual (¿elemento que cruza?).
2. Pendiente: medir la banda del split (arriba/abajo) por separado, fps y visión.

## Deuda registrada

Cadencia de 131 (diferencias muy bajas en la banda central; medir por franjas del split).
