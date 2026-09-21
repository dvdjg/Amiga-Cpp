# `eng::res` — recursos (caché de assets y código dinámico)

Gestión de recursos sobre la E/S asíncrona del mini-SO: una **caché de assets** con presupuesto,
prioridad, `refcount` y desalojo LRU, y (planificado) un **loader de código relocatable** (`.englib`).

El diseño canónico está en
[`docs/engine/architecture/RESOURCE_SYSTEM.md`](../../../../docs/engine/architecture/RESOURCE_SYSTEM.md)
y el plan en [`docs/guides/roadmap/ROADMAP_RESOURCES.md`](../../../../docs/guides/roadmap/ROADMAP_RESOURCES.md).

## Cabeceras

| Cabecera | Contenido | Estado |
|---|---|---|
| `asset_cache.hpp` | `AssetCache<Backend, MaxAssets>`: `declare`/`get`/`prefetch`/`pin`/`set_priority`/`add_ref`/`release`/`on_load_done`/`set_frame`; presupuesto Chip/Fast y desalojo por prioridad+LRU. | **Implementado** (HOST-254) |
| `dynloader.hpp` | `DynLoader`: formato `.englib` (relocs + exports), `load_async`/`unload`/`symbol`. | prevista |

El `Backend` de la caché aporta `alloc(bytes, bank)`, `free(block, bank)` y `load(id, path, dst)`;
el de Amiga usará `MemorySystem` + `os::file_read_async`, y el host un arena falsa. Reglas del
engine: sin heap en el camino caliente, sin excepciones ni RTTI, `gnu++23`.
