# HOST-384 · telemetry (panel de memoria)

Test host de `eng/debug/telemetry.hpp`: `telemetry_from(MemoryManager, MemorySystem, Telemetry)`
rellena el panel de depuración desde los **bancos** (reservas reales) + la **scratch de frame**.

## Qué valida

- `chip_used`/`chip_capacity` salen del **banco** (no de la arena): el panel muestra lo que de
  verdad está reservado.
- `chip_slots`/`chip_slots_max`: fragmentación del pool (sube al liberar un bloque del medio).
- `frame_used`/`frame_capacity`: la scratch de frame.
- `draw_telemetry` añade las líneas `SCRATCH` y `CHIPHOLES` (fragmentación).

Ver `ROADMAP_MEMORY_OWNERSHIP.md` (Fase 4) y `MEMORY_OWNERSHIP.md`.
