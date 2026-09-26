# HOST-348 — gestor de memoria central por uso (`eng::MemoryManager`)

Respalda `engine/include/eng/memory/memory_manager.hpp`: reparte la memoria disponible por
**banco** (Chip/Slow/Fast, cada uno un `BlockPool`) según el **uso** de la reserva, no el banco:

| `MemUse` | Banco (política) |
|---|---|
| `Dma` (bitplanes, copper, audio, sprites, tiles) | **Chip** (DMA de Agnus; sin fallback) |
| `Compute` (simulación, búsqueda, cómputo) | **Fast** si hay; si no Slow; si no Chip |
| `General` (buffers/tablas) | **Slow** si hay; si no Fast; si no Chip |

- `configure(chip, size, slow, size, fast, size)` — el **backend** entrega los buffers (tras sondear
  `hw::HwInfo`); los bancos pueden faltar (A500 sin Fast/Slow).
- `bank_for(use)`, `allocate(bytes, use)`/`allocate_block<Tag>(...)` con *fallback* por uso,
  `free(block)` (devuelve al banco de origen) y `free_bytes(...)`.
- Centraliza el reparto → **reasignación con control**; el medio es un **dato**, no una clase.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/348_memory_manager
```
