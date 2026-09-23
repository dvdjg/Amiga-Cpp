# HOST-077 — StaticVector y RingBuffer

Respalda `engine/include/eng/core/util/static_vector.hpp` y
`engine/include/eng/core/util/ring_buffer.hpp`.

## Qué cubre

- **`StaticVector<T, N>`**: `push_back` (rechaza al llenar), `emplace_back`
  (devuelve `nullptr` al llenar), `pop_back`, `erase` (por índice y por puntero),
  `clear`, `fill`, `front`/`back`, `at`, `span()`.
- **`RingBuffer<T, N>`**: `push` (rechaza al llenar), `push_overwrite` (descarta el
  más antiguo), `pop`, `pop_discard`, `front`/`back`, `operator[]` por antigüedad,
  `clear`, y coherencia tras vaciar y reutilizar.

## Requisitos y coste

`T` debe ser construible por defecto: los `N` huecos se construyen al declarar el
objeto (no hay `new` de colocación en el runtime freestanding). Para tipos pesados,
usar un pool/arena.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/077_static_vector_ring_buffer
```
