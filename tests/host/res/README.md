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
| HOST-340 | [block_pool](340_block_pool/README.md) | Recursos: `BlockPool` (`eng/memory/block_pool.hpp`) — pool *first-fit* con `free`/fusión sobre un buffer, genérico por medio (`MemoryKind`). |
| HOST-348 | [memory_manager](348_memory_manager/README.md) | Recursos: `MemoryManager` (`eng/memory/memory_manager.hpp`) — bundle de **bancos tipados** Chip/Slow/Fast (`MemBank`), acceso por banco sin `MemoryKind` en runtime. |
| HOST-349 | [mem_bank](349_mem_bank/README.md) | Recursos: `MemBank<Bank>` (`eng/memory/mem_bank.hpp`) — reserva **tipada por banco** (`TypedBlock<Tag,Bank>`/`Address<Bank>`); el medio va en el tipo (compile-fail si DMA recibe Fast). |
| HOST-351 | [mem_address](351_mem_address/README.md) | Recursos: `Address<K>` (`eng/core/types/memory_kind.hpp`) — **banco en el tipo** (Chip ≠ Fast) + **aritmética de dirección** (`addr+off→addr`, `addr−addr→offset`) sin perder el tipo.
