# HOST-104 — matemáticas para `Fixed` (`fixed_math.hpp`)

Respalda `engine/include/eng/core/fixed_math.hpp`: especializa `scalar_sin`/`scalar_cos`/
`scalar_sqrt` para `Fixed<s16,E>` usando la tabla de seno y `isqrt`.

## Qué cubre

- `sin`/`cos` de `q12` (tabla `SineTable<(1<<E),4096>`) contra referencias y la
  identidad `sin²+cos²=1`.
- `sqrt` de `q12` (`isqrt(raw<<E)`), incluido negativo → 0.
- **`exp2`/`log2` de `q12`** por tabla (generada en compile-time) y exponente entero;
  `exp2` satura al rango del fixed y `log2` solo es válido si el resultado cabe.
- `smooth_damp<q12>` (usa `exp2` fixed), los easings `_sine`/`_expo` y `length` con
  fixed, y `normalize`/`project`/`reflect` (ahora con `div_norm`).
- `sin`/`cos` de `MiniFloat16` (su trig de `minifloat_math`).

## Límites

`Fixed` trig requiere `E <= 14` (amplitud `2^E` en `s16`); la tabla tiene ~1 ulp de
error. `exp2` satura; `log2` en q12 solo es correcto para `x` en `[2^-8, 2^8)` (fuera
satura).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/104_fixed_math
```
