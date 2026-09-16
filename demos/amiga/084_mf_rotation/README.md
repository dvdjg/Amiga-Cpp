# Demo 084 — rotación 3D en `MiniFloat16` (`minifloat_math`)

Valida **en hardware** (68000, sin soft-float) la cadena

```
   minifloat_math (sin/cos, exp, sqrt)  ->  Mat<3, MiniFloat16>
   ->  eng/retro/minifloat_fixed (transform de coordenadas q0)  ->  Bresenham
```

dibujando un **cubo alambre que gira** sobre un display EHB (mismo *canvas* planar que
la 077), con las aristas sombreadas por profundidad.

## Qué enseña (técnica exclusiva)

La rotación de la cámara se construye **íntegramente con `MiniFloat16`**: los ángulos se
llevan a seno y coseno con `eng::math::sin`/`cos`, se componen `Rz·Ry·Rx` con `Mat<3,
MiniFloat16>` (producto de matrices en MF) y los vértices (coordenadas `q0`, píxeles
enteros) se transforman con `eng::retro::transform`, que hace el producto escalar
fusionado (la matriz MF se lleva a 4.12 una vez y acumula en 32 bits). Es decir: **las
matemáticas de coma flotante de 16 bits del engine mueven geometría en un A500 real**,
sin `__mulsf3`/`__addsf3`.

## Self-test de trascendentes

En `init` la demo comprueba `sin(π/2)≈1`, `exp(0)≈1` y `sqrt(4)≈2`; si fallaran en 68000,
la demo iría a **Failed** en vez de Ready. Un `analyze` en verde implica que la
trigonometría/exponencial del escalar de 16 bits funciona en el chip.

## Invariantes

- El cálculo (ángulos, matrices, transform) va en `update` (antes del vblank); `render`
  (durante el vblank) solo traza líneas: así no hay *tearing* por escribir Chip RAM con
  el DMA activo.
- Los ángulos se pliegan a `[-2π, 2π]` (`wrap_2pi`), el dominio fiable de `sin`/`cos`.
- El `transform` exige razones en `[-8, 8]` (4.12); una rotación cumple de sobra.

## Comandos

```bash
bash ./tools/build/build-demo.sh demos/amiga/084_mf_rotation --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/084_mf_rotation
bash ./tools/analyze/analyze-demo.sh demos/amiga/084_mf_rotation
```
