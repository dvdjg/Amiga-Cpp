# Inconsistencias de propiedad y memoria

## Estado

Abierto. La migración de consumidores hacia `MemoryManager` y los tipos de banco está avanzada, pero el ciclo de vida persistente todavía no coincide con la arquitectura objetivo de `MEMORY_OWNERSHIP.md`.

## Alcance

Este hallazgo cubre la relación entre `MemorySystem`, `MemoryManager`, `MemBank`, `BlockPool`, `LinearArena`, `Assets`, `AssetCache` y los recursos DMA. Registra qué criterios siguen abiertos después de las correcciones parciales de ownership y lifecycle, para evitar que la documentación presente como cerrado un comportamiento que aún no lo está.

## Hallazgos

| ID | Severidad | Inconsistencia | Evidencia | Impacto |
|---|---|---|---|---|
| MEM-001 | Crítica | El backend Amiga configura pools propios para `MemoryManager`, pero mantiene arenas raíz separadas para `MemorySystem`/scratch; los consumidores que pierdan el `Block` siguen sin poder liberar su reserva. | `engine/src/platform/amiga/amiga.cpp:94-108`; `engine/include/eng/memory/block_pool.hpp:88-105,152-155` | La integración de owners y el teardown ordenado siguen sin recuperar recursos individualmente de forma segura. |
| MEM-002 | Cerrado parcialmente | `AssetCacheBackend::free()` libera el `MemoryBlock` según `MemoryKind`; la caché expone vistas no propietarias con handle `{id, generation}`. | `engine/include/eng/platform/amiga/asset_backend.hpp`; `engine/include/eng/res/asset_cache.hpp` | El bloque y la vista de caché quedan ligados a su banco y generación; los consumers deben conservar leases al retenerlas. |
| MEM-003 | Cerrado | `AssetCache::AssetSlot` conserva `MemoryBlock {data,size,kind}` y el tamaño físico asignado. | `engine/include/eng/res/asset_cache.hpp:59-71,331-369` | Owner físico y banco efectivo permanecen disponibles para liberar y contabilizar correctamente. |
| MEM-004 | Alta | `Assets::create()` entrega un `Block`, pero no lo registra en `m_tracked`; el llamador debe liberar manualmente y no hay destructor de `Assets` que haga teardown. | `engine/include/eng/api/assets.hpp:84-121` | La propiedad automática prometida no existe para recursos creados directamente. |
| MEM-005 | Cerrado | `Assets::reset_phase()` libera los bloques y vacía `AssetTable`; `AssetTable` almacena nombre/datos como `StringView`/`Span` no propietarios, sin generación ficticia. | `engine/include/eng/api/assets.hpp`; `engine/include/eng/res/asset_table.hpp`; HOST-386/353 | Las consultas tras reset no recuperan rangos liberados; el caller de `AssetTable` independiente debe mantener vivos los rangos que presta. |
| MEM-006 | Alta | `MemBank::release(const Block&)` invalida el bloque recibido por referencia constante, pero no impide que existan vistas copiadas o que el llamador conserve el valor. | `engine/include/eng/memory/mem_bank.hpp:60-69` | La seguridad depende del diagnóstico; la transferencia de ownership no queda expresada en la firma. |
| MEM-007 | Cerrado | `Any` solicita Fast y el slot contabiliza el `MemoryKind` efectivo del bloque (Fast o Slow). | `engine/include/eng/res/asset_cache.hpp:330-369`; `engine/include/eng/platform/amiga/asset_backend.hpp:35-67`; HOST-330 | Contabilidad, presupuesto y liberación usan el mismo banco efectivo. |
| MEM-008 | Cerrado parcialmente | El bloque conserva el tamaño reservado del pool y el slot mantiene por separado el tamaño lógico de datos. | `engine/include/eng/res/asset_cache.hpp:67-71,331-369`; `engine/include/eng/memory/block_pool.hpp:109-136` | La reserva alinea internamente el pool; el presupuesto de caché cuenta el tamaño físico del `MemoryBlock`. |
| MEM-009 | Parcial; resto fuera del frente activo | Leases move-only bloquean evict/shutdown; `AssetDmaLease` exige Chip, Paula espera a DMA idle en teardown y `play_sfx_asset` conserva la muestra CPU durante la voz. | `engine/include/eng/res/asset_cache.hpp`; `engine/include/eng/audio/audio_system.hpp`; HOST-254/330 | El handle sigue siendo responsabilidad del scope consumidor; Blitter/Copper no adquieren ni prolongan automáticamente una lease. Reabrir solo con un consumidor y un defecto reproducible; no se construye infraestructura preventiva en este roadmap. |
| MEM-010 | Media | `from_storage()` se usa en varias capas de dominio, aunque la arquitectura lo reserva a fronteras certificadas. | `engine/include/eng/graphics/bob.hpp`, `engine/include/eng/field/*.hpp`, `engine/include/eng/scene/*.hpp` | La procedencia Chip se audita por convención y no por una API con owner verificable. |
| MEM-011 | Cerrado | El gate de codegen crea o usa `out/tmp` y pasa sin libcalls de multiplicación/división ni instrucciones 68020. | `tools/analyze/codegen-report.mjs`; salida de regresión actual | La verificación de portabilidad del código generado es reproducible. |

## Revisión diferida fuera del roadmap de memoria

`HOST-382` (`tests/host/audio/382_audio_compressor_cli`) falla en la vertical ACP1 y nunca ha funcionado de forma confiable; queda marcado para revisar en el roadmap de audio-compresor. No forma parte del cierre de ownership/lifecycle y no se investigó en esta pasada.

## Documentación desfasada

| Documento | Afirmación que debe corregirse |
|---|---|
| `docs/guides/roadmap/ROADMAP_MEMORY_OWNERSHIP.md` | Debe distinguir el pool propio actual de `MemoryManager` del ownership incompleto fuera de `AssetCache`, y mantener abiertos rollback y teardown global. |
| `docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md` | Presenta `MemoryManager` y `MemBank` como único asignador productivo, aunque la cadena Amiga comparte cursor con `MemorySystem`. También presenta la frontera `from_storage` como más restringida de lo que refleja el código. |
| `docs/engine/architecture/RESOURCE_SYSTEM.md` | Contrastar el diseño de API con `MemoryBlock`, `MemoryKind`, handles generacionales y leases DMA implementadas. |
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

El hallazgo se cierra cuando el backend productivo usa un pool liberable para reservas persistentes, todos los owners conservan su bloque, el banco efectivo se contabiliza correctamente, todas las vistas observables se invalidan al liberar y cada uso DMA mantiene una lease hasta completar. La suite debe incluir desalojo real, destrucción automática, doble liberación, generación inválida, banco efectivo y liberación con trabajo DMA pendiente.

## Referencias

- [`MEMORY_OWNERSHIP.md`](../../engine/architecture/MEMORY_OWNERSHIP.md)
- [`ROADMAP_MEMORY_OWNERSHIP.md`](../../guides/roadmap/ROADMAP_MEMORY_OWNERSHIP.md)
- [`INTERNAL_TYPE_SYSTEM.md`](../../engine/architecture/INTERNAL_TYPE_SYSTEM.md)
- [`RESOURCE_SYSTEM.md`](../../engine/architecture/RESOURCE_SYSTEM.md)
