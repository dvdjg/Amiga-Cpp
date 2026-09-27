# HOST-364 — un único asignador por medio (bancos enlazados a las arenas)

Respalda `BlockPool::configure_backing` (`eng/memory/block_pool.hpp`) y
`MemoryManager::configure_backing` (`eng/memory/memory_manager.hpp`): los bancos de Chip/Slow
**delegan en las arenas del `MemorySystem`** — mismo buffer y **mismo cursor** — de modo que reservar
desde la arena y desde el banco **no se solapa**.

- Era el bug del backend Amiga: `configure_memory` entregaba el **mismo** buffer a `MemorySystem` y
  `MemoryManager` con cursores **independientes**, y `res::load` (assets) pisaba el bitmap/copperlist
  de `compose` (banda de basura en una escena).
- Con el *backing* enlazado, `allocate` del banco reserva de la arena (`LinearArena::allocate`);
  `capacity()`/`free_bytes()`/`kind()` reflejan la arena.
- El banco deja de **reciclar** (`free` es no-op): la arena es *bump*, así que `release` no rebobina
  el cursor. No hay consumidor que dependa de `release` real en los bancos.
- La variante `configure(base, ...)` con **buffers propios** sigue viva para tests host que no
  comparten medio.

Cubre: reservas consecutivas de arena y banco **disjuntas**, avance del cursor de la arena con la
reserva del banco, `capacity`/`free_bytes`/`kind` delegados, y `configure(base, ...)` aislado.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/364_allocator_sharing
```
