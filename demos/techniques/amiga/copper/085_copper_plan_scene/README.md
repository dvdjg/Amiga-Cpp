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
- **BOB (disco)**: un disco que se mueve por la pantalla en Lissajous y **comunica su
  necesidad** a la escena: su degradado (`COLOR01`) tiene que quedar **anclado a su Y**, así
  que aporta intenciones de color por scanline.

Las dos fuentes se añaden al plan **en cualquier orden** (la demo mete antes el BOB y
después el cielo) y el plan las **ordena por línea relativa al inicio del display**
(`PlanConfig::first_line = 0x2c`; el listado del Copper envuelve a 256 líneas). El BOB y el
cielo usan **registros distintos** (`COLOR01` frente a `COLOR00`) para no pisarse.

```
  BOB (se mueve)  --\
                     >-- intents -->  copper::Plan: ordena por scanline (SETUP)
  CIELO (fijo)    --/                 lista estática en 2 bloques (uno por bitmap)
                                      por frame: patch de slots -> flip -> commit
```

## Detalles de implementación

- **Bitmap doble-buffered a mano** (2 bloques de planos): el BOB se dibuja en el trasero y
  la lista del plan apunta a ese con `emit_planes_display`.
- **Lista estática + parcheo por slots**: la lista se materializa **una vez por bitmap** en
  el setup; los intents del BOB se emiten para **todas** las líneas que el disco puede ocupar
  (posiciones fijas) y el cielo usa las bandas fijas del efecto. Por frame solo se parchean
  las **palabras de DATO** (cielo con `RasterGradientEffect::bind_slots`/`patch_words`; BOB
  con el rango actual ∪ anterior: arcoíris dentro del disco, color base fuera). El
  `materialize`/`sort` se paga solo en el setup (antes: 194k ciclos/frame).
- **BOB por Blitter**: el disco se **prerenderiza** en 16 variantes (offset de PALABRA de
  `x`) y cada frame se envían **dos blits** — borrado de la caja anterior (copia desde una
  fila de ceros repetida con módulo `-8`) y copia de la variante alineada a palabra (sin
  shift). El CPU solo programa registros; la copia byte a byte a Chip costaba ~101k ciclos.
- **Los intents no llevan offsets cableados**: el plan guarda los handles del emisor
  (`slot_count`/`slot_word`/`slot_reg`/`slot_line`).

## Rendimiento

**49,92 fps / 1,00 field / 142.102 ciclos/frame** (era 16,62 fps / 3,01 fields / 426.733).
Perfil por secciones de la ruta vigente: blits del disco 20,6k + parcheo 17,0k + flip/commit
0,5k; el resto es espera de VBlank. Atribución completa de la ruta previa (materialize 194k,
dibujo CPU 101k, resto de build 119k) y las lecciones generales:
`docs/guides/optimization/ATRIBUCION_DE_CICLOS.md`.

## Verificación

- `analyze-screenshot.sh` → `verify_c2p_color` (≥12 colores, croma y cobertura): 24 colores.
- **Gate de visión obligatorio** (secuencia fresca): cielo en bandas + disco moviéndose **con
  arcoíris de degradado vertical en su interior**, sin restos del disco anterior. Nota: pasar
  varias imágenes en una sola llamada puede confundir al modelo (describió «dos discos» una
  vez sobre una secuencia **vieja** del directorio); re-capturar antes de concluir.

## Build & run & analyze

```bash
tools/build/build-demo.sh demos/techniques/amiga/copper/085_copper_plan_scene --clean
tools/run/run-demo.sh       demos/techniques/amiga/copper/085_copper_plan_scene
tools/analyze/analyze-demo.sh demos/techniques/amiga/copper/085_copper_plan_scene
```
