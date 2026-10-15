# VALIDATION — Demo 101 (`ehb_tile_scroll_driver`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA (cadencia con anomalía)** — deuda registrada abajo.

Intención esperada: scroll de tiles EHB con driver por `BPLCON1` fino + puntero de anillo;
cadencia uniforme de 1 px/frame sin tirones.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps debug | `node tools/debug/measure-fps.mjs 101_ehb_tile_scroll_driver A500_debug --json` | **49,92 fps · 1,00 field · 142.102 ciclos** | presupuesto cumplido |
| cadencia (6 frames consecutivos, banda y=64..191) | `run-demo.sh … --sequence-step-frames 6` + `out/tmp/cadence.mjs 0 64 320 128 frame_*.png` | pares: **30,6 \| 50,7 \| 0,0 \| 30,4 \| 30,5** (repetidos=1) | **anomalía**: un par sin movimiento (0,0) y otro con salto ~1,7× → tirón visible |

## Conclusiones

1. fps en presupuesto (1 campo) ✓.
2. **Cadencia uniforme en fase de movimiento** (pares ~29,5-30,6 en 12 frames); la ruta de la
   cámara en la fase 0 es **exactamente 1 px/frame** (`RouteCamera::advance`, `apply_phase(0)`:
   `wx = min_x + t`). El par **0,0 + 50,7** aparece una vez por cruce de palabra y **es un
   artefacto de la captura por paso** (el congelado en el ready-probe frente al swap de
   `COP1LC`), no del render: el analizador propio de la demo
   (`analyze-fine-scroll.sh`, fine 14→15→0→1) da `shifts=2,2,2` con el **mismo WARN** en el
   primer par (`medidos=0,2,2`) y termina **OK fine scroll**.
3. Pendiente: visión de la secuencia (no bloqueante).

## Deuda registrada

Artefacto de captura en el cruce de palabra (0,0 + salto) compartido con 104 — anotado como
nota de herramienta; el contrato fino de 101 lo cubre su propio analizador (OK).
