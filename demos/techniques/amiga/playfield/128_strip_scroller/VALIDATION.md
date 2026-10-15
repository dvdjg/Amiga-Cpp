# VALIDATION — Demo 128 (`strip_scroller`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: VERIFICADA (cadencia y fps en el alcance medido)** — visión de secuencia pendiente.

Intención esperada: scroll de tiras (anillo + guarda + cobertura) con parcheo de `BPLCON1`/`BPLxPT`.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps debug | `node tools/debug/measure-fps.mjs 128_strip_scroller A500_debug --json` | **49,92 fps · 1,00 field · 142.102 ciclos** | presupuesto cumplido |
| cadencia (6 frames consecutivos, banda y=64..191) | `run-demo.sh … --sequence-step-frames 6` + `out/tmp/cadence.mjs 0 64 320 128 frame_*.png` | pares: **23,8 \| 23,9 \| 23,9 \| 23,8 \| 23,7** | cadencia **uniforme** (variación < 1 %) |

## Conclusiones

1. fps en presupuesto (1 campo) y **cadencia uniforme** ✓ en el alcance medido (banda central,
   6 frames consecutivos).
2. Sin verificar aquí: visión de la secuencia (artefactos/huecos en la costura) y las zonas
   fuera de la banda medida.
3. El parcheo por frame de la copperlist no introduce tirones medibles.

## Deuda registrada

Visión de secuencia de 128 (pendiente, no bloqueante).
