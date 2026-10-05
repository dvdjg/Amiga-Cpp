# Referencia — caché de assets y presupuesto

La app pide por `AssetId`; si no cabe, los assets no fijados, no referenciados y de menor prioridad **salen solos** (desalojo LRU). La caché es **pura** respecto a E/S y memoria: recibe un `Backend` con `alloc`/`free`/`load` (el de Amiga usa `MemorySystem` + `os::file_read_async`; el de host, un arena falsa).

## `AssetCache<Backend, MaxAssets>` — `res/asset_cache.hpp`

| Tipo | Qué es |
|---|---|
| `AssetId` (`asset_cache.hpp:20`) | Id (`0` = inválido). |
| `AssetHandle` (`:23`) | Handle **no propietario**; la **generación** invalida copias después de evict/reload. |
| `AssetView` (`:31`) | Vista no propietaria (`data` + `handle` + `kind`) verificable contra `AssetCache::valid`. |
| `AssetState` (`:41`) | `Empty`/`Loading`/`Ready`/`Error`. |
| `MemoryRequest` (`:45`) | Restricción de bancos pedida por el recurso: `Any`/`NoChip`/`Chip`/`Fast`/`Slow` (`MemoryKind` describe **dónde está** el bloque, no la petición). |
| `AssetSlot` (`:48`) | Un slot (`path`/`state`/`request`/`priority`; 255 = casi nunca se desaloja). |
| `AssetLease`/`AssetDmaLease` (`:66`, `:117`) | Lease Chip que el llamador mantiene viva hasta completar el plan. |
| `CacheConfig` (`:120`) | Presupuesto por banco (Chip/Fast). |

La carga se completa por el llamador con `on_load_done(id, result, generation)`. Ver `docs/engine/architecture/RESOURCE_SYSTEM.md` §1.

## Presupuesto — `res/budget.hpp`

`Budget` (`budget.hpp:25`) es una vista de **solo lectura** sobre los bancos del `MemoryManager` para decidir si un recurso **cabe antes de pedirlo**: `used_chip`/`remaining_chip`/`capacity_chip` (y Slow/Fast), y `can_fit(bytes, kind)` (`:52`) — `Fast` sin banco cae a Slow (como `fast_or_slow`); `Any`/`Chip` van a Chip. No posee memoria ni reserva nada.

## Carga tipada — `res/load.hpp`

`load<Tag>(mm, src)` (`load.hpp:94`) copia bytes a un `Block<Tag>` en la arena que corresponde al dominio, sustituyendo el patrón `allocate_block<Tag>` + `memcpy`. El **medio y la alineación** los fija `DomainAsset<Tag>` (`:41`; especializaciones para `PlaneTag`/`BobTag`/`CopperTag`/`MusicTag`/`AudioTag`, `:46`): los datos que consume DMA van a Chip, planos y copperlists piden alineación 16. Si no cabe, devuelve un bloque **inválido**. `load_file<Tag>(mm, path[, out_bytes])` (`:117`, `:143`) es la variante de fichero. `kLoadHeadroom` (`:36`) ya no se usa con `MemBank` (el `BlockPool` alinea una vez).

## Tabla de assets en memoria — `res/asset_table.hpp`

`AssetTable` (`asset_table.hpp:25`, `kMaxAssets = 16`) registra blobs incrustados (`INCBIN`/`INCBIN_CHIP`) por **nombre y dominio**: `add<Tag>(name, data)` (`:31`) y `get<Tag>(name)` (`:39`, `ByteView<Tag>`), sin `__asm__` ni punteros en la lógica. `valid_view` (`:50`) audita que la vista sigue señalando los mismos bytes.

```cpp
INCBIN_CHIP(abyss_img, "assets/amiga/sprites/abyss/abyss.bpl");
assets.add<eng::PlaneTag>("abyss", incbin_abyss_img_start, bytes);
auto img = assets.get<eng::PlaneTag>("abyss");   // ByteView<PlaneTag>
```

## Runtime de assets — `res/asset_runtime.hpp`

`AssetRuntime<CacheBackend, MaxAssets>` (`asset_runtime.hpp:35`) junta la **caché** con el **enrutado de E/S** (`route_io`) para que el bucle cargue de disco de forma asíncrona sin conocer el backend: `init(backend, cfg)` (`:41`), `load(path, size, request)` (`:52`) declara y lanza, `on_msg(m)` completa con los `FileDone`/`FileError` que drena el bucle, y `state(id)`/`get(id)` consultan. El runtime **posee** el backend (la caché solo guarda una referencia).

## Enrutado — `res/resources.hpp`

`route_io(m, cache, libs)` (`resources.hpp:21`) enruta los mensajes de E/S discriminando por `IoUser::tag` (`kTagAsset = 'A'`, `kTagLib = 'L'`, `kTagStream = 'S'`, `:15`): una sola cola sirve a todos los recursos sin cruzar consumidores, y la **generación** del cookie rechaza respuestas tardías (R6.2).

Volver al [índice de `res/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
