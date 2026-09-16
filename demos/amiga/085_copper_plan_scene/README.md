# Demo 085: escena con el copper orquestado por `copper::Plan`

Demuestra **para qué sirve** el plan de copper del engine
(`engine/include/eng/graphics/copper/plan.hpp`, contrato en
`docs/engine/architecture/DISPLAY_COMPOSITION.md` §5): una escena con **varias fuentes de
copper** que cambian a ritmos distintos y deben intercalarse por scanline **sin que el
llamador las ordene a mano**.

## Qué se ve

- **Cielo (copper sky)**: 16 franjas horizontales de `COLOR00` con un degradado
  (azul → violeta → naranja → rojo) que cicla. Es una aportación **de la escena**: fija,
  de fondo.
- **BOB (software)**: un disco que se mueve por la pantalla en Lissajous y **comunica su
  necesidad** a la escena: su degradado (`COLOR01`) tiene que quedar **anclado a su Y**, así
  que aporta 3 intenciones que se mueven cada frame (arriba / medio / abajo del disco).

Las dos fuentes se añaden al plan **en cualquier orden** (la demo mete antes el BOB y
después el cielo) y el plan las **ordena por línea relativa al inicio del display**
(`PlanConfig::first_line = 0x2c`; el listado del Copper envuelve a 256 líneas). El BOB y el
cielo usan **registros distintos** (`COLOR01/03/05` frente a `COLOR00`) para no pisarse.

```
  BOB (se mueve)  --\
                     >-- intents -->  copper::Plan: ordena por scanline
  CIELO (fijo)    --/                        materializa en el bloque TRASERO
                                             commit() -> swap de COP1LC
```

## Detalles de implementación

- **Bitmap doble-buffered a mano** (2 bloques de planos): el BOB se dibuja en el trasero y
  la lista del plan apunta a ese con `emit_planes_display`.
- **BOB clásico**: el disco se **prerenderiza** en 8 variantes (una por offset sub-byte de
  `x`) y cada frame solo se **copian bytes**; borrado por caja a nivel de byte. Sin esto el
  dibujo por píxel costaba ~6 fps.
- **Los intents no llevan offsets cableados**: el plan guarda los handles del emisor.

## Rendimiento

~25 fps (2 campos/frame) en WinUAE-DBG `-O1`. El frame queda justo por encima de un campo,
así que el bucle lo sincroniza a dos; el coste está en el copiado del BOB a Chip RAM y en
la emisión del plan, no en el Copper (que trabaja en paralelo).

## Verificación

- `analyze-screenshot.sh` → `verify_c2p_color` (≥12 colores, croma y cobertura).
- **Gate de visión obligatorio** (secuencia): un disco moviéndose sobre el cielo de franjas,
  sin duplicados ni parpadeo. Nota: pasarle varias imágenes en una sola llamada confunde al
  modelo (describió «dos discos» cuando es el mismo disco en dos posiciones); conviene
  revisar un frame suelto si señala anomalías.

## Build & run & analyze

```bash
tools/build/build-demo.sh demos/amiga/085_copper_plan_scene --clean
tools/run/run-demo.sh       demos/amiga/085_copper_plan_scene
tools/analyze/analyze-demo.sh demos/amiga/085_copper_plan_scene
```
