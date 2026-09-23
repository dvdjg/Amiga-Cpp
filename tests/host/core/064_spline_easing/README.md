# HOST-064 — splines y easing genéricos

Respalda `eng/core/spline.hpp` (Hermite cúbica y Catmull-Rom) y los easing de
`eng/core/interp.hpp` sobre el escalar: los MISMOS algoritmos con `double`,
`MiniFloat16`, `q12` (fixed 4.12) y `Fixed<s32,12>`, contra una referencia en `double`
calculada desde las MISMAS entradas redondeadas.

## Qué cubre

- **Hermite** (`h00..h11`): error rel `0` (double), `~1e-3` (MF) y `~4e-4` (q12).
- **Catmull-Rom**: pasa por los puntos de control (`t=0 → p1`, `t=1 → p2`) y error
  `~2.5e-3` (MF) / `~1.3e-3` (q12). Usa `div_norm` para el factor 1/2 (funciona en fixed).
- **Easing** `ease_in/out/in_out_quad` y `_cubic`: error `~1e-3` (MF/q12), extremos
  exactos (`0→0`, `1→1`).
- **Easing senoidal/exponencial** (`_sine`/`_expo`) con `double`/`float`/`MiniFloat16` y
  `Fixed<s32,12>` (`~6e-3`). El `_expo` no se prueba con `q12` porque usa las constantes
  10/20, que no caben en 4.12.

## Cómo está implementado

`mul_norm` normaliza cada producto al escalar (identidad en `float`/`MiniFloat16`,
`rescale` en fixed), así que el mismo código vale para todos. Los easing son polinómicos
(sin división) salvo Catmull-Rom, que usa la división explícita `div_norm`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/064_spline_easing
```
