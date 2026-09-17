# HOST-076 — Array y BitSet

Respalda `engine/include/eng/core/util/array.hpp` y
`engine/include/eng/core/util/bitset.hpp`.

## Qué cubre

- **`Array<T, N>`** (agregado, `std::array`): inicialización con llaves,
  `size`/`empty`/`capacity`, `operator[]`, `at` (con parada `illegal` en m68k),
  `front`/`back`, iteradores, `fill`, `swap`, `operator==` (y `!=` reescrito),
  `span()`.
- **`BitSet<N>`** (`std::bitset`): `set`/`reset`/`flip` (por bit y completos),
  `test`, `count`, `any`/`none`/`all`, `clear`, `words()`, `operator==`.

## Por qué un `BitSet` propio

Palabras de 32 bits exactos (`__UINT32_TYPE__`) en vez de `unsigned long`, para que
el ancho sea el mismo en m68k y en el host. Los bits por encima de `N` en la última
palabra no cuentan en `count`/`all`/`any`, aunque se invierta todo con `flip()`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/076_array_bitset
```
