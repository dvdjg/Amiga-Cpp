# HOST-074 — operaciones de bits

Respalda `engine/include/eng/core/util/bit.hpp`, el equivalente freestanding de
`<bit>`.

## Qué cubre

- **Conteo**: `popcount`, `countl_zero`, `countr_zero`, `countl_one`, `countr_one`,
  `bit_width`, `one_bit`.
- **Potencias de dos**: `has_single_bit` (delega en `eng::is_pow2`), `bit_floor`,
  `bit_ceil` (devuelve 0 si el resultado no cabe).
- **Rotación**: `rotl`/`rotr` con normalización de `k` módulo el ancho (incluye
  `k = 0` y `k = ancho`).
- **Endian**: `bswap16`, `bswap32`.
- **Reinterpretación**: `bit_cast` (solo con `__builtin_bit_cast` disponible).

## Por qué el ancho importa

`eng::u32` es `unsigned long`, que mide 4 bytes en m68k **y en el host MinGW** de
estos tests, pero 8 en Linux. Por eso las funciones enmascaran al ancho exacto de
`T` y corrigen los builtins de conteo por la diferencia de anchura: `popcount` de
`s8(-1)` es 8, no 32, y `countl_zero` de un `u16` cuenta dentro de 16 bits.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/074_bit_ops
```
