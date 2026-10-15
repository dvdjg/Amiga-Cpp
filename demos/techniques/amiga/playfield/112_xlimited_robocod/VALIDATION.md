# VALIDATION — Demo 112 (`xlimited_robocod`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA por rendimiento** — deuda registrada abajo.

Intención esperada: scroll XYLimited estilo Robocod (X fino + Y), cadencia uniforme a 50 fps.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps debug | `node tools/debug/measure-fps.mjs 112_xlimited_robocod A500_debug --json` | **12,07 fps · 4,14 fields · 587.786 ciclos** | 4× el presupuesto |
| cadencia (6 frames consecutivos, banda y=64..191) | `run-demo.sh … --sequence-step-frames 6` + `out/tmp/cadence.mjs 0 64 320 128 frame_*.png` | pares: **5,5 \| 5,5 \| 5,4 \| 5,5 \| 5,5** | cadencia **uniforme** (el movimiento por frame es constante) |

## Conclusiones

1. **Cadencia uniforme** ✓ (diff casi idéntico en los 5 pares): no hay tirones relativos; el
   problema es de **coste**, no de patrón.
2. **NO VERIFICADA por rendimiento**: 4,14 fields/frame. El movimiento es uniforme pero lento
   (12 fps). Pendiente perfil por secciones (`profile.mjs`) para atribuir el coste, con las
   lecciones de `docs/guides/optimization/ATRIBUCION_DE_CICLOS.md`.
3. Visión de la secuencia pendiente.

## Deuda registrada

Rendimiento de 112 (12,07 fps / 4,14 fields) — perfilar por secciones y reducir.
