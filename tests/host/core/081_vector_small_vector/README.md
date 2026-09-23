# HOST-081 — Vector y SmallVector

Respalda `engine/include/eng/core/util/vector.hpp` y
`engine/include/eng/core/util/small_vector.hpp`.

## Qué cubre

- **`Vector<T, A>`**: crecimiento sobre un asignador (`BumpAlloc`), `reserve`/`insert`/
  `erase`/`resize`/`move`, semántica de movimiento (el origen queda vacío) y rechazo
  controlado cuando el buffer no da más (devuelve `false`, no aborta).
- **`SmallVector<T, N, A>`**: capacidad inline hasta `N` sin tocar el asignador
  (`is_inline()`), primer desbordamiento al asignador, y el caso `NullAlloc` (no
  desborda).

## Restricción

`T` debe ser **copiable trivialmente**: el almacenamiento no construye ni destruye
objetos (no hay `new` de colocación en freestanding), que es el caso de los tipos de
valor del engine.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/081_vector_small_vector
```
