# HOST-340 — pool de bloques con `free` (`eng::BlockPool`)

Respalda `engine/include/eng/memory/block_pool.hpp`: asignador **first-fit** con **fusión de huecos**
sobre un buffer del llamador (sin heap), que complementa la arena *bump* (`LinearArena`, sin
`free`) para consumidores que **reciclan** memoria (nametables/CHR que cambian) — el
memoria reutilizable (p. ej. de un consumidor externo). Cubre:

- alineación (`align`), `free_bytes()`, `capacity()`;
- `free` + **fusión** de bloques contiguos (`block_count()`);
- rechazo (`nullptr`) cuando no cabe o el pool está vacío.

Es **general** (cualquier juego con buffers reutilizables).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/340_block_pool
```

Ver `ROADMAP_API_COHERENCE.md` §7 (F7.6).
