# Pendiente de verificar (documentación)

Notas de cosas que quedaron sin hacer en el hilo de documentación, para retomar.

## Documentar como playground el backend de Amiga

- `engine/include/eng/platform/amiga_minimal.hpp` (`MinimalBackend`, `HardwareProfile`,
  `DebugOverlay`, `FlatTriangle`, `ServiceSlot`, `DirectToken`, `LineEorParams`,
  `OrBobEntry`, `C2p4State`, miembros `m_*`) está **parcialmente documentado** (aplicado el
  turno de la regla de miembros). Falta: dejar escrito en la doc canónica que
  `MinimalBackend` es una **especie de playground** para prototipar/probar funcionalidad
  hardware y, si madura, incorporarla al engine (no es la API final del backend). Sitio
  candidato: `docs/engine/architecture/GRAPHICS_DRIVERS.md` o un README de `platform/`.
- Revisar si `OrBobBatch`, `ServiceSlot::ctx` y otros `T*` internos del backend deben pasar
  a `eng::Ref<T>` (como ya se hizo con `DirectToken::ctx`).
- Documentar los miembros de `engine/include/eng/memory/arena.hpp`, `platform/amiga/*`
  (`blob.hpp`, `lib3d.hpp`, `object3d.hpp`, `polygon_fill.hpp`, `gfx3d.hpp`).

## Orden de includes en runner host (temporal)

- `out/tmp/npc-hosttests.sh` (overlay, no versionado) debe pasar `-Iout/tmp/hostinc`
  **antes** de `-Iengine/include` para que el fallback de `__is_pointer`/`__is_function`
  del g++ 13 de WSL se resuelva. El script ya lo hace; verificar que no se rompe si se
  regenera.

## `scene::compose` sin `install()` / `commit()` del plan

- `Scene::commit()` NO publica la copperlist (no llama a `Plan::commit`/`DoubleBuffer::install`).
  En 040/050/051/052/060/100_virtual se eliminó el `install()` por frame asumiendo que la
  copperlist es estática; **verificar en WinUAE** que el display sigue siendo correcto (no
  quedó evidencia de captura para 050/051/052/060/100_virtual más allá de 050). Si el
  `Scene` debe publicar cambios estáticos una vez, añadir un `Scene::install(backend)`.
