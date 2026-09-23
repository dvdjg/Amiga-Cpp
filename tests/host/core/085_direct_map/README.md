# HOST-085 — DirectMap

Respalda `engine/include/eng/core/util/direct_map.hpp`.

## Qué cubre

- Mapa de **clave densa** `0..N-1` con acceso `O(1)` (array indexado + `BitSet<N>` de
  presencia): `insert`/`insert_or_assign`/`find`/`contains`/`erase`/`for_each`.
- Claves **fuera de rango** rechazadas sin abortar (`find` → `nullptr`, `insert` →
  `nullptr`, `insert_or_assign` → `false`).
- `at()` sobre clave ausente detiene la CPU (no se prueba en el test porque termina el
  proceso).
- Valores no escalares (un `struct Pixel`) sobre el almacenamiento tipado.

## Uso previsto

Claves densas del engine: índice de tile, id de canal de sprite, índice de plano. Es
el caso más barato posible (sin hash ni comparaciones).

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/085_direct_map
```
