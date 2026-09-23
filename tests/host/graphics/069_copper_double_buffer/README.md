# HOST-069: `copper::DoubleBuffer` y su uso por `TileScrollScene`

Test host de `engine/include/eng/graphics/copper/double_buffer.hpp`, la **implementación
única del doble buffer de copperlist** que antes estaba repetida en `TileScrollScene` y en
los compositores de field (F1 de `docs/guides/roadmap/NORMALIZACION_REPO.md`).

## Qué comprueba

**A) API del `DoubleBuffer`**
1. Reserva dos bloques distintos; `active_index()` arranca en 1 (el inactivo, 0, es el
   primero que se escribe).
2. `flip()` alterna activo/inactivo.
3. `takeover`/`install` publican el bloque **activo**.
4. `begin` rechaza un tamaño inservible.

**B) Integración con `TileScrollScene` (EHB)**
5. El primer `rebuild` emite la lista completa y `install` publica un bloque.
6. Un `rebuild_copper(scroll)` **parchea el bloque inactivo** (`BPLCON1` y `BPLxPT`) y
   publica **el otro** bloque.
7. **Invariante**: el bloque que deja de estar activo conserva los valores viejos — no se
   ha tocado la lista que el Copper estaba ejecutando.

Los valores se localizan **escaneando la lista por registro** (no por offsets cableados),
que es justo la fragilidad que el refactor elimina: ahora el driver guarda los *handles*
que devuelve `Scheduler::move_at`.

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/graphics/069_copper_double_buffer
```
