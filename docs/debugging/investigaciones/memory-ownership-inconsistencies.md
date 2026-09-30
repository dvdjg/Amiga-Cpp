# Inconsistencias de propiedad y memoria

## Estado

Abierto. La migración de consumidores hacia `MemoryManager` y los tipos de banco está avanzada, pero el ciclo de vida persistente todavía no coincide con la arquitectura objetivo de `MEMORY_OWNERSHIP.md`.

## Alcance

Este hallazgo cubre la relación entre `MemorySystem`, `MemoryManager`, `MemBank`, `BlockPool`, `LinearArena`, `Assets`, `AssetCache` y los recursos DMA. No describe una solución implementada; sirve para evitar que la documentación presente como cerrado un comportamiento que aún no lo está.

## Hallazgos

| ID | Severidad | Inconsistencia | Evidencia | Impacto |
|---|---|---|---|---|
| MEM-001 | Crítica | El backend Amiga configura pools propios para `MemoryManager`, pero mantiene arenas raíz separadas para `MemorySystem`/scratch; los consumidores que pierdan el `Block` siguen sin poder liberar su reserva. | `engine/src/platform/amiga/amiga.cpp:94-108`; `engine/include/eng/memory/block_pool.hpp:88-105,152-155` | La integración de owners y el teardown ordenado siguen sin recuperar recursos individualmente de forma segura. |
| MEM-002 | Crítica | `AssetCacheBackend::free()` no libera el bloque recibido. | `engine/include/eng/platform/amiga/asset_backend.hpp:45-46` | El desalojo LRU solo cambia el estado del slot; la memoria permanece ocupada. |
| MEM-003 | Alta | `AssetCache::AssetSlot` conserva `Span<u8>`, no un handle o bloque propietario. | `engine/include/eng/res/asset_cache.hpp:27-38` | El backend no puede aplicar una liberación tipada ni conservar el tamaño físico reservado. |
| MEM-004 | Alta | `Assets::create()` entrega un `Block`, pero no lo registra en `m_tracked`; el llamador debe liberar manualmente y no hay destructor de `Assets` que haga teardown. | `engine/include/eng/api/assets.hpp:84-121` | La propiedad automática prometida no existe para recursos creados directamente. |
| MEM-005 | Alta | `Assets::reset_phase()` libera bloques pero mantiene en `AssetTable` vistas a memoria liberada. | `engine/include/eng/api/assets.hpp:123-140` | Un acceso posterior puede usar memoria liberada; falta invalidación por generación o vaciado coordinado. |
| MEM-006 | Alta | `MemBank::release(const Block&)` invalida el bloque recibido por referencia constante, pero no impide que existan vistas copiadas o que el llamador conserve el valor. | `engine/include/eng/memory/mem_bank.hpp:60-69` | La seguridad depende del diagnóstico; la transferencia de ownership no queda expresada en la firma. |
| MEM-007 | Alta | `AssetCache::Any` se convierte a `Fast` en `start_load`, pero `fast_or_slow` puede reservar realmente en Slow cuando no hay Fast. | `engine/include/eng/res/asset_cache.hpp:234-249`; `engine/include/eng/memory/memory_manager.hpp:77-85` | Contabilidad, banco efectivo y liberación pueden divergir. |
| MEM-008 | Media | La caché contabiliza `s.size`, mientras el pool reserva el tamaño alineado de `MemoryBlock`. | `engine/include/eng/res/asset_cache.hpp:225-230`; `engine/include/eng/memory/block_pool.hpp:109-136` | El presupuesto puede no reflejar el consumo físico real. |
| MEM-009 | Media | No existe una comprobación centralizada de DMA pendiente antes de desalojar un asset o liberar un bloque. | `engine/include/eng/res/asset_cache.hpp:215-232`; contrato en `MEMORY_OWNERSHIP.md` | Posible use-after-free del Blitter, Copper o Paula. |
| MEM-010 | Media | `from_storage()` se usa en varias capas de dominio, aunque la arquitectura lo reserva a fronteras certificadas. | `engine/include/eng/graphics/bob.hpp`, `engine/include/eng/field/*.hpp`, `engine/include/eng/scene/*.hpp` | La procedencia Chip se audita por convención y no por una API con owner verificable. |
| MEM-011 | Media | El gate completo de host llega al análisis de codegen y falla porque no existe `out/tmp`, no por un diagnóstico de C++ identificado. | `tools/run-host-tests.sh`; salida de regresión del pull | La verificación global no es reproducible hasta preparar el directorio temporal. |

## Documentación desfasada

| Documento | Afirmación que debe corregirse |
|---|---|
| `docs/guides/roadmap/ROADMAP_MEMORY_OWNERSHIP.md` | Debe distinguir el pool propio actual de `MemoryManager` del ownership incompleto de `AssetCache`, y mantener abiertos rollback, teardown y DMA pendiente. |
| `docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md` | Presenta `MemoryManager` y `MemBank` como único asignador productivo, aunque la cadena Amiga comparte cursor con `MemorySystem`. También presenta la frontera `from_storage` como más restringida de lo que refleja el código. |
| `docs/engine/architecture/RESOURCE_SYSTEM.md` | Describe una caché que pide y libera bloques, pero el backend real no libera y no conserva bloques propietarios. |
| `docs/engine/architecture/PUBLIC_GAME_API.md` | Presenta `load<T>`/handles como objetivo y mezcla `HwInfo`/`LinearArena` en el presupuesto; el código vigente expone `MemoryManager`/`Budget` y todavía no implementa `app.load<T>`. |
| `docs/engine/architecture/MEMORY_OWNERSHIP.md` | Especifica el objetivo correctamente, pero debe marcar explícitamente los criterios 1, 3 y 6 como pendientes mientras existan MEM-001..MEM-007. |
| `docs/engine/architecture/MEMORY_MODEL.md` | Describe `ChipArena` como asignador de recursos persistentes; debe distinguir pool persistente de `FrameScratch`. |
| `docs/engine/architecture/DISPLAY_COMPOSITION.md` | La firma de ejemplo usa `MemorySystem`, mientras los consumidores migrados usan `MemoryManager`. |
| `docs/engine/architecture/GAME_AUDIO.md` | La firma de `AudioSystem::init` usa `MemorySystem`, aunque la ruta actual usa bancos de `MemoryManager`. |
| `docs/engine/architecture/OBJECT_SYSTEM.md` | Atribuye la propiedad del framebuffer a `MemorySystem`/`Block`; debe reflejar `Scene`/`Bitmap` y el banco gestionado. |
| `docs/engine/architecture/HARDWARE_AND_ROM_KERNEL_POLICY.md` | Presenta la gestión actual exclusivamente como arenas; ya existe `MemoryManager`/`BlockPool`, aunque la integración productiva aún es híbrida. |
| `docs/engine/architecture/ENGINE_DESIGN.md` | El inventario de `eng::memory` omite `MemoryManager`, `MemBank`, `BlockPool` y `ScratchArena`. |
| `docs/engine/architecture/ROADMAP_API_COHERENCE.md` | Conserva como diagnóstico actual la ausencia de arena Fast y el mapeo Fast→Slow, y no separa la inconsistencia de ownership de la migración de API. |

## Criterio de cierre

El hallazgo se cierra cuando el backend productivo usa un pool liberable para reservas persistentes, `AssetCacheBackend::free()` devuelve el bloque correcto, `Assets` y los assets cacheados tienen ownership automático, el banco efectivo (`Fast`/`Slow`) se resuelve antes de contabilizar, los handles se invalidan al terminar una fase y las liberaciones esperan a que no exista DMA pendiente. La suite debe incluir pruebas de desalojo real, destrucción automática, doble liberación, generación inválida, banco efectivo y liberación con trabajo DMA pendiente.

## Referencias

- [`MEMORY_OWNERSHIP.md`](../../engine/architecture/MEMORY_OWNERSHIP.md)
- [`ROADMAP_MEMORY_OWNERSHIP.md`](../../guides/roadmap/ROADMAP_MEMORY_OWNERSHIP.md)
- [`INTERNAL_TYPE_SYSTEM.md`](../../engine/architecture/INTERNAL_TYPE_SYSTEM.md)
- [`RESOURCE_SYSTEM.md`](../../engine/architecture/RESOURCE_SYSTEM.md)
