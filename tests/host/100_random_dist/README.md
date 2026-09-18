# HOST-100 — distribuciones de aleatoriedad

Respalda las utilidades de `engine/include/eng/core/random.hpp` (además del PRNG
`Xoroshiro64pp`).

## Qué cubre

- `next_range` (`[lo,hi)`), `next_symmetric` (`[-range,range]`), `chance(num,den)`.
- `pick` (elemento al azar de un `Span`), `shuffle` (Fisher-Yates in situ).
- `next_gaussian_approx` (suma de 4 uniformes, campana entera, sin `float`/libm).
- Determinismo (misma semilla = misma secuencia) y cobertura del rango.

## `next_mod` sin división

`Xoroshiro64pp::next_mod` usa **máscara + rechazo** para `n` no potencia de dos (y
máscara directa si lo es), evitando `__umodsi3`/`__umulsi3` en el 68000; por eso la
sonda `c_random_ops` no muestra libcalls.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/100_random_dist
```
