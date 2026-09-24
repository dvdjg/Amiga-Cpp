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

## Rendimiento (abierto)

**16,7 fps** (3 campos/frame) con el BOB de 45 intenciones. Medido con el contador de
ciclos: el `update` cuesta poco (BOB ~19k ciclos con el Blitter, plan ~45k con ~300
intenciones) y sin embargo el **bucle** se lleva ~425k ciclos por frame. Dos experimentos
lo acotan y quedan como trabajo pendiente:

- **Cielo con cambio por línea (256 intenciones)**: el frame se dispara a **14
  campos/frame**. Es la mejora que falta (degradado suave de verdad) y el motivo por el que
  hoy el cielo son 16 bandas.
- **Relleno de polígono por Blitter** para el BOB: el coste de CPU baja (19k frente a los
  97k de la copia byte a byte a Chip RAM) pero el frame sube a 3 campos → apunta a
  **contención de bus** (los blits del polígono/máscara con la CPU compitiendo por el bus en
  ciclo-exacto), no al coste de programación.

Para cerrarlo hace falta perfilar el bucle (no el update): VPOSR/`wait_vblank` con el
Copper y el Blitter activos, y probar alternativas (blit directo del blob enmascarado en
vez de polígono + cookie-cut, o BOB en fast RAM).

## Verificación

- `analyze-screenshot.sh` → `verify_c2p_color` (≥12 colores, croma y cobertura): 24 colores.
- **Gate de visión obligatorio** (secuencia): cielo en bandas + disco moviéndose **con
  arcoíris de degradado vertical en su interior**. Nota: pasarle varias imágenes en una
  sola llamada confunde al modelo (describió «dos discos» y luego «no hay duplicados»);
  conviene revisar un frame suelto si señala anomalías.

## Build & run & analyze

```bash
tools/build/build-demo.sh demos/amiga/085_copper_plan_scene --clean
tools/run/run-demo.sh       demos/amiga/085_copper_plan_scene
tools/analyze/analyze-demo.sh demos/amiga/085_copper_plan_scene
```
