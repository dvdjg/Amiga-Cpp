# VALIDATION — Demo 202 (`xlimited_dpf`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA** — deuda abierta en `docs/guides/roadmap/ROADMAP_DEUDA_TECNICA.md` **DT-006**.

Intención: demo de dual-playfield con scroll XLimited (familia del corkscrew Y-limited).

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| barrido por bandas | `node tools/analyze/check-elements.mjs out/run/202_xlimited_dpf/A500_debug/sequence ffffff frame_000.png frame_011.png` | **y=448..575 del PNG (2×) con `diff=0.00` → zona inferior estática** (misma firma que la 110) | comparte el bug de la familia (DT-006) |
| fps | `measure-fps 202_xlimited_dpf A500_release` | **no resuelto** (`map`/`runtime=0x0`, DT-004): pendiente de medir | sin dato |
| captura de paso | `run-demo.sh … --sequence-step-frames` | disponible (`frame_NNN_fNNNN.png`) | usar en la próxima pasada |

## Conclusión

Comparte la geometría rota del corkscrew (DT-006): la zona baja del display no se actualiza. No se
declara correcta ninguna parte; el fix del engine (ficha `docs/reference/amiga/techniques/ylimited-corkscrew.md`)
la cubrirá junto con la 110 y la 201, y entonces se repetirán estas medidas.

## Deuda registrada

DT-006 (bandas inferiores de la familia XLimited) · DT-004 (`measure-fps` sin resolver símbolos en 202 release).

## Cadencia (2026-10)

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps debug | `node tools/debug/measure-fps.mjs 202_xlimited_dpf A500_debug --json` | **28,45 fps · 2,01 fields · 285.673 ciclos** (DT-009) | 2 campos por frame; excepción documentada pendiente |
| cadencia (6 frames consecutivos, banda y=64..191) | `run-demo.sh … --sequence-step-frames 6` + `out/tmp/cadence.mjs 0 64 320 128 frame_*.png` | pares: **48,8 \| 48,9 \| 48,9 \| 48,9 \| 49,0** | cadencia **uniforme** (sin tirones relativos) |

La banda central se actualiza de forma uniforme; el movimiento es regular pero a 2 campos por
frame (28 fps). El defecto de la zona inferior (DT-006) sigue abierto.
