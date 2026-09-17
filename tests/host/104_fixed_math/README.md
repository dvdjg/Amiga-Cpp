# HOST-104 — matemáticas para `Fixed` (`fixed_math.hpp`)

Respalda `engine/include/eng/core/fixed_math.hpp`: especializa `scalar_sin`/`scalar_cos`/
`scalar_sqrt` para `Fixed<s16,E>` usando la tabla de seno y `isqrt`.

## Qué cubre

- `sin`/`cos` de `q12` (tabla `SineTable<(1<<E),4096>`) contra referencias y la
  identidad `sin²+cos²=1`.
- `sqrt` de `q12` (`isqrt(raw<<E)`), incluido negativo → 0.
- `stddev<q12>` (que ya compila gracias a `scalar_sqrt<Fixed>`) y los easings
  `ease_in_sine`/`ease_out_sine` con fixed.
- `length<q12>` (usa `sqrt`); se documenta que `normalize`/`project` no compilan con
  fixed (usan `operator/`, prohibido en el núcleo).
- `sin`/`cos` de `MiniFloat16` (su trig de `minifloat_math`).

## Límites

`Fixed` trig requiere `E <= 14` (amplitud `2^E` en `s16`); la tabla tiene ~1 ulp de
error. `exp2` **no** se especializa para fixed.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/104_fixed_math
```
