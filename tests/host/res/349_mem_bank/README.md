# HOST-349 — banco de memoria tipado (`eng::MemBank`) + protección compile-time

Respalda `engine/include/eng/memory/mem_bank.hpp` y el acceso tipado de `MemoryManager`: un
`MemBank<Bank>` (especialización por `MemoryKind`) entrega **reservas ya tipadas por el banco** —
`TypedBlock<Tag, Bank>` con `Address<Bank>`.

- El **medio va en el tipo** (etiqueta vacía, coste cero): `Address<Chip>` y `Address<Fast>` son
  tipos distintos.
- Una API que exige DMA (Chip) acepta `Address<Chip>` y **no compila** con `Address<Fast>`
  (fijado con un `concept` + `static_assert`).
- `reserve` es miembro del **banco** (no del bloque): `bank.reserve<Tag>(bytes)`.
- El banco se conoce en **runtime** (tamaños en el setup): un banco sin bytes (Fast/Slow en un
  A500) devuelve bloques **inválidos**.
- `MemoryManager::chip()/slow()/fast()` dan los bancos tipados (tag de plantilla, sin `MemoryKind`
  en runtime).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/349_mem_bank
```
