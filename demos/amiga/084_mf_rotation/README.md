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

- El cálculo (ángulos, matrices, transform) va en `update`; `render` solo traza líneas.
- **No se borra un rectángulo cada frame**: se borran *las aristas del frame anterior* y se
  trazan las nuevas, así el display nunca queda vacío a mitad de frame (un `clear_rect`
  hacía que la captura cogiera el hueco) y se ahorra escribir toda la zona.
- Los ángulos se pliegan a `[-2π, 2π]` (`wrap_2pi`), el dominio fiable de `sin`/`cos`.
- El `transform` exige razones en `[-8, 8]` (4.12); una rotación cumple de sobra.

## Telemetría (periférico de depuración)

`compute_projection` publica en el periférico `0xB70000` los ciclos **emulados** del
cálculo MF por frame: `counter 0` = total, `counter 1` = matriz (`6 sin/cos` + `2
Mat*Mat`). Se leen con:

```bash
bash ./tools/run/run-demo.sh demos/amiga/084_mf_rotation --read-debugperiph counters
```

Medición (WinUAE, `cpu_cycle_exact`): el cálculo MF pasó de **190 236** a **127 926**
ciclos/frame (−33%) con dos cambios:
- `sincos` por eje en vez de `sin`+`cos` (una sola reducción de rango): 190k → 158k.
- `prepare_ratio` (convertir la matriz MF a 4.12 UNA vez y aplicarla a los 8 vértices en
  vez de reconvertirla por vértice): 158k → 128k.

El resto del presupuesto es el trazado de líneas. Con 128k ciclos el cálculo MF cabe en
los ~141k de un frame a 50 fps.

## Comandos

```bash
bash ./tools/build/build-demo.sh demos/amiga/084_mf_rotation --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/084_mf_rotation
bash ./tools/analyze/analyze-demo.sh demos/amiga/084_mf_rotation
```
