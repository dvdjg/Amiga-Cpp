# HOST-093 — estadística básica

Respalda `engine/include/eng/core/util/stats.hpp`.

## Qué cubre

- `sum`, `mean`, `variance` (poblacional, **Welford** en una pasada), `stddev`.
- `kth_smallest`/`median` (copian a `scratch` y usan `nth_element`; sin heap).
- `histogram` por ancho fijo (devuelve cuántos cayeron fuera).
- `ema` (media móvil exponencial) y `RunningMean<S, N>` (ventana deslizante).
- Los **mismos algoritmos** con `double`, `MiniFloat16` y `q12` (fixed), contra
  referencias conocidas; `stddev` solo para escalares con `sqrt` (no `Fixed`).
- `sum`/`mean` usan el **acumulador ancho declarado por el escalar**
  (`scalar_traits<S>::wide_t`): `s32` para `q12` y `s64` para `Fixed<s32,12>` (host),
  saturando al estrechar; dos muestras al máximo de `s32` no envuelven.

## Límites

La división va por `div_norm` (explícita): `Fixed` satura y `MiniFloat16` lleva ~1e-3
de error (por eso la tolerancia de MF es más ancha). `kth_smallest`/`median` **mutan**
el `scratch` del llamador.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/093_stats
```
