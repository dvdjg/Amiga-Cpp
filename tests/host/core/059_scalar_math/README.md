# HOST-059 — base matemática genérica (escalar-independiente)

Respalda `eng/core/math/numeric_traits.hpp`, `eng/core/math/interp.hpp` y `eng/core/math/geometry.hpp`:
los MISMOS algoritmos se ejercitan con `double` y con `MiniFloat16`, comparando contra
una referencia en `double` calculada a partir de las **mismas entradas redondeadas**.

## Qué cubre

- **Rasgos y guards de compilación** (`numeric_traits`): `static_assert`s de
  `range_fits<MiniFloat16,-6.29,6.29>`, `!range_fits<q12,-10,10>`, `has_division` de cada
  escalar, `max_finite`/`has_nan` de MF. Es el mecanismo con el que un uso fuera de rango
  del escalar falla **al compilar**.
- **Interpolación** (`interp.hpp`): `lerp`, `smoothstep`, `smootherstep`, `remap`,
  `inv_lerp`, `saturate` con `double` (exacto a 1e-6) y `MiniFloat16` (lerp ~2.7e-3,
  easing ~1.5e-2).
- **Geometría** (`geometry.hpp`): `dot`, `cross2`, `perp`, `rotate2`, `length`,
  `distance`, `normalize` (`|n|≈1`), `project`/`reject` (`project+reject=v`), `reflect`,
  `vlerp` y `clamp` de vector, con `double` y `MiniFloat16`.

## Cómo está estructurado

Ver [`docs/engine/architecture/SCALAR_LIBRARY.md`](../../../../docs/engine/architecture/SCALAR_LIBRARY.md):
algoritmos genéricos header-only + rasgos del escalar (`scalar_traits`, `numeric_traits`,
`scalar_sqrt`) como puntos de extensión; las optimizaciones concretas son
especializaciones (de CPU, de escalar), y el `.cpp` queda para lo no-plantilla (backend).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/059_scalar_math
```
