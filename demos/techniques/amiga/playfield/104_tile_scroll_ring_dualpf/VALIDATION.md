# VALIDATION — Demo 104 (`tile_scroll_ring_dualpf`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA (cadencia escalonada)** — deuda registrada abajo.

Intención esperada: scroll de anillo de tiles en dual playfield, cadencia uniforme.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps debug | `node tools/debug/measure-fps.mjs 104_tile_scroll_ring_dualpf A500_debug --json` | **38,79 fps · 1,29 fields · 182.885 ciclos** | por debajo de 50 (algunos frames de 2 campos) |
| cadencia (6 frames consecutivos, banda y=64..191) | `run-demo.sh … --sequence-step-frames 6` + `out/tmp/cadence.mjs 0 64 320 128 frame_*.png` | pares: **0,0 \| 0,0 \| 0,0 \| 64,9 \| 64,8** (repetidos=3) | **patrón escalonado**: 3 frames sin cambio + 2 saltos grandes → el scroll avanza a saltos (≈ cada 4 frames) |

## Conclusiones

1. fps por debajo del presupuesto (1,29 fields): algún frame ocupa 2 campos.
2. **La ventana inicial (frames 23-28) cayó en la PAUSA INICIAL DISEÑADA** (`pause_frames = 30`
   en la cámara de la demo): los 3 pares 0,0 % eran la pausa, no un fallo. Recapturado en fase
   de movimiento (frames 125-130, fase 0 = `wx = m_phase_wx + t`, 1 px/frame): pares
   **64,9 | 64,8 | 64,9** uniformes + un par **0,0 + 80,4** = el **mismo artefacto de captura
   del cruce de palabra** que en 101 (congelado en el ready-probe frente al swap de `COP1LC`).
3. Pendiente: caracterizar el 1,29 fields (frames de 2 campos) y visión de secuencia.

## Deuda registrada

Fps de 104 (1,29 fields) — perfilar los frames lentos. Artefacto de captura compartido con 101
(nota de herramienta).
