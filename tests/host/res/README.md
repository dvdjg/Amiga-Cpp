# Tests HOST — res

Categoría `res` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-012 | [assets_uaf](012_assets_uaf/README.md) | `eng/assets/uaf.hpp`: contenedor UAF-R (`Blob`/`Reader`/vistas) — bind/find/data + errores. |
| HOST-248 | [dynloader](248_dynloader/README.md) | Recursos: `DynLoader` (`eng/res/dynloader.hpp`) — `.englib` relocatable (relocaciones + símbolos). |
| HOST-254 | [asset_cache](254_asset_cache/README.md) | Recursos: `AssetCache` (`eng/res/asset_cache.hpp`) — ciclo, presupuesto, desalojo por prioridad/LRU y `pin`/`refcount`. |
| HOST-255 | [io_route](255_io_route/README.md) | Recursos: `IoUser` (cookie) y `route_io` (`eng/res/resources.hpp`) — enrutado de `FileDone`/`FileError` por `tag` sin cruzar consumidores. |
| HOST-258 | [hunk_loader](258_hunk_loader/README.md) | Recursos: cargador **HUNK** (`eng/res/hunk.hpp`) — segmentos en `LinearArena`, relocaciones (32/32SHORT) y símbolos; detección de formato `.englib`/HUNK en `DynLoader`. |
| HOST-325 | [budget](325_budget/README.md) | Recursos: `Budget` (`eng/res/budget.hpp`) — presupuesto de memoria (used/remaining/capacity, `can_fit` Chip/Slow/Fast) sobre las arenas del `MemorySystem`. |
| HOST-326 | [load](326_load/README.md) | Recursos: `res::load<Tag>` (`eng/res/load.hpp`) — carga tipada a `Block<Tag>` con medio/alineación por dominio (`DomainAsset`), copia y rechazo por overflow. |
| HOST-328 | [load_file](328_load_file/README.md) | Recursos: `res::load_file<Tag>` (fichero, `os::file_*`) — abrir/medir/reservar/leer/cerrar con backend falso en memoria; caminos de error (no existe, lectura corta, overflow). |
| HOST-331 | [asset_runtime](331_asset_runtime/README.md) | Recursos: `res::AssetRuntime` (`AssetCache` + `route_io`) — `load` (declara + lanza), `on_msg(FileDone)`, `get` y descarte de mensajes ajenos. |
| HOST-332 | [asset_typed](332_asset_typed/README.md) | Recursos: `AssetRuntime::bytes<Tag>` — vista de dominio (`ByteView<Tag>`) de un asset cargado: vacía antes de `Ready`, con tamaño/datos/alineación después. |
| HOST-340 | [chip_pool](340_chip_pool/README.md) | Recursos: `ChipPool` (`eng/res/chip_pool.hpp`) — pool *first-fit* con `free`/fusión sobre un buffer (memoria reutilizable; `IChipMem`). |
