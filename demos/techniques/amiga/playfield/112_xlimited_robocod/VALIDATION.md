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

### Publicación (swap de `COP1LC`) y contrato pixel

La instalación de la copperlist vivía en `render`, justo después del `wait_vblank` del bucle
(`engine.hpp:180`), y había además un bloque muerto `#ifdef K_EARLY_INSTALL` en `update`: el patrón
cuya carrera con el latch documenta la demo 101 (latch de `COP1LC` en el wrap `V=maxvpos→0`,
WinUAE-DBG `custom.cpp:11237-11245`; detalle en `101…/VALIDATION.md` y `DISPLAY_COMPOSITION.md`
§6.12). Se unificó en un único punto al final de `update` (margen dentro del campo;
`install_copper_list` solo escribe `COP1LC`, nunca `COPJMP1`).

Evidencia: `--pixel-assert` verde — `shift-left-1px-per-frame` dx=−1, 8 frames, 0 duplicados
(informe `out/regression/20261016-134500`); sobre los mismos frames de la captura de visión,
`assert-pixel-contract.sh` da `worst=0.016` (tolerancia 0,05). **Nota de herramienta**: el modelo
de visión local (qwen3-vl 8B) **no detecta** el paso de 1 px en este patrón de puntos de alta
frecuencia y reporta los frames como «idénticos» (falso negativo comprobado: el contrato numérico
y la comparación directa de rayas diagonales/barras confirman el desplazamiento). Para pasos
finos con patrón de alta frecuencia, la evidencia válida es el contrato numérico más la
comparación directa de frames.
