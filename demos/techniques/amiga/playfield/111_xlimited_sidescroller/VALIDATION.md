# VALIDATION — Demo 111 (`xlimited_sidescroller`)

Informe de validación (F4/F5 de `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md`).
**Estado: NO VERIFICADA por rendimiento** — deuda registrada abajo.

Intención esperada: sidescroller XYLimited (scroll fino 2 px/frame por `BPLCON1` sobre mapa
toroidal con racha de tiles) con nave de vaivén vertical; cadencia uniforme y sin tirones.

## Mediciones

| Medida | Comando | Resultado | Lectura |
|---|---|---|---|
| fps debug | `node tools/debug/measure-fps.mjs 111_xlimited_sidescroller A500_debug --json` | **16,59 fps · 3,01 campos/frame · 427.590 ciclos** | 3× el presupuesto (deuda de coste) |
| contrato pixel (paso de frame) | `tools/test-regression.sh --demo … --warp --pixel-assert` | `shift-left-2px-per-frame` dx=−2: **8 frames, 0 pares duplicados, 7 pares desplazados**; informes `out/regression/20261016-134355` y `…-134454` (estadística idéntica) | paso de 2 px por frame, determinista |
| visión (secuencia por paso, 8 frames) | `run-demo.sh … --warp --sequence-step-frames 8` + `tools/profile/ollama-analyze.mjs out/profile/111_step --mode frames` | par a par: fondo desplazado ~2 px a la izquierda, nave moviéndose en vertical, **sin huecos negros ni tearing** (`out/profile/111_step/ollama-report.md`) | movimiento y aspecto OK |

## Conclusiones

1. **Movimiento correcto en el alcance probado**: el contrato `dx=−2` por paso, la visión de la
   secuencia por paso y la comparación directa de frames consecutivos (`f0009`→`f0010`) coinciden:
   el mapa completo avanza 2 px por frame, sin campos congelados ni saltos.
2. **NO VERIFICADA por rendimiento**: 3,01 campos/frame (~16,6 fps). Deuda: perfil por secciones
   para atribuir el coste (mismo tipo de deuda que 112).

## Deuda registrada

1. Rendimiento: 3,01 campos/frame — perfilar y reducir (objetivo 1 campo/frame).

### Publicación (swap de `COP1LC`): orden fijado con margen

La instalación de la copperlist vivía en `render`, justo después del `wait_vblank` del bucle
(`engine.hpp:180`), a ~1 scanline del wrap: el mismo patrón cuya carrera con el latch documenta la
demo 101 (latch de `COP1LC` en el wrap `V=maxvpos→0`, WinUAE-DBG `custom.cpp:11237-11245`; detalle
en `101…/VALIDATION.md` y `DISPLAY_COMPOSITION.md` §6.12). Se movió al final de `update`, con
margen dentro del campo: `install_copper_list` solo escribe `COP1LC` (nunca `COPJMP1`), la lista en
curso no se toca y la recarga ocurre en el VBlank siguiente. Evidencia: contrato verde en la
pasada previa (133640) y en las dos pasadas posteriores idénticas (134355/134454).
