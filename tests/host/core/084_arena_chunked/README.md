# HOST-084 — ArenaAlloc y ChunkedVector

Respalda `engine/include/eng/core/util/arena_alloc.hpp` y
`engine/include/eng/core/util/chunked_vector.hpp`.

## Qué cubre

- **`ArenaAlloc`**: adaptador del `LinearArena` del engine al contrato `Allocator`.
  Se ejercita con `Vector` (reservas reales sobre la arena, contabilidad de `used` y
  rechazo de una reserva imposible).
- **`ChunkedVector<T, Chunk, MaxChunks, A>`**: crecimiento por bloques con **direcciones
  estables** (se comprueba que `&cv[0]` no cambia al crecer), mapeo por bloques, `full`,
  `clear` que conserva y reutiliza los bloques, y el caso sin allocator (`NullAlloc`),
  que no puede crear el primer bloque.

## Uso previsto

Datos referenciados por punteros/handles estables (streaming, caches de tiles, listas
que el Blitter ya apunta) y pruebas de fase `init` sobre la memoria del backend.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/084_arena_chunked
```
