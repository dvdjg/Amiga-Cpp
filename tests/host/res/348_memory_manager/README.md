# HOST-348 — bancos de memoria del engine (`eng::MemoryManager`)

Respalda `engine/include/eng/memory/memory_manager.hpp`: un **bundle de bancos tipados**
(`MemBank<Chip>`/`<Slow>`/`<Fast>`). Cada reserva sale de un banco **concreto** (tag de plantilla
`MemoryKind`), **sin** `MemoryKind` en runtime ni `switch`: `reserve`/`release`/`free_bytes` son
miembros del banco.

- `configure(chip, size, slow, size, fast, size)` — el backend entrega los buffers (tras sondear
  `hw::HwInfo`); los bancos pueden faltar (A500 sin Fast/Slow → 0 bytes → reservas inválidas).
- `chip()/slow()/fast()` — bancos tipados; `has_slow()/has_fast()`.
- Un uso «general» se resuelve eligiendo el **banco en el código** (compile-time), no con dispatch.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/348_memory_manager
```
