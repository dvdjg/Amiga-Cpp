# Roadmap del sistema de recursos (`eng::res`)

Plan de la capa de recursos descrita en
[`RESOURCE_SYSTEM.md`](../../engine/architecture/RESOURCE_SYSTEM.md): **caché de assets** con
presupuesto/prioridad/LRU y **loader de código relocatable**, sobre la E/S asíncrona del mini-SO
([`MINI_OS_IO.md`](../../engine/architecture/MINI_OS_IO.md), fases M7/M8 de
[`ROADMAP_MINI_OS.md`](ROADMAP_MINI_OS.md)).

## Principios

- **Componer, no duplicar.** La E/S y los mensajes ya existen (mini-SO); la caché y el loader solo
  añaden **política de memoria** y **formato relocatable**.
- **HOST primero.** La política (selección de víctima, presupuestos, estados, enrutado por `tag`) es
  pura y host-testable con una E/S simulada; el hardware, con demos.
- **Sin heap en el camino caliente**; capacidad fija (slots y libs), arenas del `MemorySystem`.
- **La app no ve punteros ni handles de OS**: pide por `AssetId`/`LibHandle`.

## Fases

### R0 — VFS completo

- **Entregable**: `eng/os/file.hpp` con `FileMode::Create`, `file_delete`, `file_rename`; cookie de
  E/S `IoUser { tag, id }` y `FileOp` con Create/Delete/Rename.
- **Verificación**: **HOST-244** — sobre un backend de ficheros simulado, create/open/read/write/
  delete/rename devuelven el resultado esperado y el `FileDone` lleva el `IoUser` correcto.
- **Estado**: pendiente.

### R1 — AssetCache: núcleo

- **Entregable**: `eng/res/asset_cache.hpp` (`AssetId`, `AssetSlot`, estados `Empty/Loading/Ready/
  Error`, `declare`/`get`/`try_get`/`prefetch`/`prefetch_many`, `add_ref`/`release`, `pin`,
  `set_priority`, `on_file_done`, `set_frame`).
- **Verificación**: **HOST-245** — `get` lanza carga y devuelve `nullptr`; al llegar `FileDone`
  pasa a `Ready` y `get` devuelve datos; `add_ref`/`release` y `pin` se reflejan en el estado.
- **Estado**: **entregado** (`eng/res/asset_cache.hpp` con `Backend` de `alloc`/`free`/`load`;
  cubierto junto con R2 por **HOST-254**).

### R2 — Política de desalojo

- **Entregable**: `ensure_space` + `pick_victim` (menor prioridad, luego LRU) con presupuestos
  Chip/Fast y `MemBank::Any`; `AssetEvicted` opcional.
- **Verificación**: **HOST-246** — con presupuesto pequeño, al pedir un asset nuevo se desaloja el
  de menor prioridad; a igualdad de prioridad, el más viejo; los fijados y referenciados nunca se
  desalojan; sin víctima, la carga falla con `AssetError`.
- **Estado**: **entregado** (`ensure_space`/`pick_victim`: menor prioridad y, a igualdad, LRU;
  `pin`/`refcount` protegen; HOST-254).

### R3 — Mensajes de recurso e integración con el bucle

- **Entregable**: `MsgType::AssetReady/AssetEvicted/AssetError`; `set_frame` en VBlank; `Resources`
  enruta `FileDone` por `IoUser::tag` (caché/loader/stream).
- **Verificación**: **HOST-247** — el enrutado por `tag` entrega cada `FileDone` al subsistema
  correcto y no cruza consumidores; `AssetReady` se postea una vez por asset.
- **Estado**: pendiente.

### R4 — DynLoader (código relocatable)

- **Entregable**: `eng/res/dynloader.hpp` (`LibHandle`, `declare`/`load_async`/`unload`,
  `add_ref`/`release`, `symbol`), formato `.englib` (header + relocs + exports) y `relocate`;
  `MsgType::LibLoaded/LibError/LibUnloaded`.
- **Verificación**: **HOST-248** — un `.englib` de prueba (generado en el host) se carga, se
  relocaliza y `symbol` devuelve una función que se llama; `unload` solo libera con `refcount==0`.
- **Estado**: pendiente.

### R5 — Fachada y ejemplo por zonas

- **Entregable**: `eng::res::Resources` (cache + libs + `on_msg`/`begin_frame`) y un ejemplo de
  **transición de zona** con prefetch/pin/release y carga/descarga de overlay.
- **Verificación**: **demo 210_zone_resources** — al cruzar un *trigger* se prefetchan los assets de
  la zona, se carga su `.englib` y, con poco presupuesto, se desaloja lo viejo sin bloquear el frame.
- **Estado**: pendiente.

## Tests y demos previstos

| ID | Tipo | Contenido |
|---|---|---|
| HOST-244 | test | VFS completo (create/delete/rename) e `IoUser`. |
| HOST-245 | test | AssetCache: estados y ciclo de carga asíncrona. |
| HOST-246 | test | Desalojo: prioridad + LRU + presupuestos; pin/refcount. |
| HOST-247 | test | Mensajes de recurso y enrutado por `tag`. |
| HOST-248 | test | DynLoader: `.englib`, relocación, símbolos y unload. |
| 210_zone_resources | demo | Transición de zona: prefetch + LRU + overlay de código. |

## Riesgos y decisiones abiertas

- **Presupuesto por banco.** Chip y Fast tienen costes distintos (Agnus no ve Fast): la caché debe
  respetar `MemBank` y no meter buffers de Paula en Fast.
- **Código en RAM.** En 68000 no hay NX; preferir Fast para `.englib` y validar que el rango es
  ejecutable. El formato y las relocaciones deben ser verificables por el host.
- **Formato del asset.** La caché no conoce el contenido: solo bytes. El decodificado va como tarea
  de fondo (`BACKGROUND_TASKS.md`), no en la caché.
- **Granularidad de prioridad.** Prioridad fija por tipo (tiles/música/SFX) frente a prioridad por
  zona; empezar por la fija y ajustar con perfiles de zona.
- **Paths vs hash.** En builds finales, `declare` puede tomar un hash u32 en vez de una cadena para
  no arrastrar nombres.

## Estado

Todas las fases están **pendientes**. El diseño está fijado y depende de M7 (E/S asíncrona) del
roadmap del mini-SO.
