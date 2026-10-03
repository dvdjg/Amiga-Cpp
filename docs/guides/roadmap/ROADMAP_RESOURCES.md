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
- **Estado**: **casi entregado**. Entregado: el **contrato** `eng/os/file.hpp` y su implementación
  Amiga sobre **`dos.library`** (`file_open`/`close`/`read_sync`/`write_sync`/`make_dir`/`delete`/
  `rename`; **demo 211** verifica lectura de directorios + escritura + relectura), más el cookie
  `IoUser` (**HOST-255**). Pendiente: `trackdisk` (sin DOS) y un test host de las operaciones sobre
  un backend simulado.

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
- **Estado**: **parcial**. Entregado: el **enrutado** `eng/res/resources.hpp` (`route_io` por
  `IoUser::tag`; **HOST-255**). Pendiente: los mensajes de recurso
  (`AssetReady`/`AssetEvicted`/`AssetError`) y su posteo desde la caché.

### R4 — DynLoader (código relocatable)

- **Entregable**: `eng/res/dynloader.hpp` (`LibHandle`, `declare`/`load`/`unload`, `symbol`,
  `format`), formato `.englib` (header + relocs + exports), formato **HUNK** nativo
  (`eng/res/hunk.hpp`) y `relocate`; `MsgType::LibLoaded/LibError/LibUnloaded`.
- **Verificación**: **HOST-248** (`.englib`: relocaciones + símbolos) y **HOST-258** (HUNK:
  segmentos en `LinearArena`, `HUNK_RELOC32`/`RELOC32SHORT`, `HUNK_SYMBOL`); **demo 211_fs_test**
  carga y ejecuta los **dos** formatos en la Amiga (`answer()` → 42).
- **Estado**: **parser/relocator entregado; ownership de módulos pendiente** (`DynLoader` con
  **detección de formato** `.englib`/HUNK, relocaciones y símbolos por hash FNV-1a; **HOST-248** y
  **HOST-258**; demo 211 con ambos). `load()` recibe memoria del llamador, HUNK usa una arena única y
  `unload()` no devuelve segmentos; falta el **generador host**, loader por segmento/banco, ownership,
  carga por path y pipeline comprimido. Ver R6 y `FILE_SYSTEM_AND_DYNAMIC_LIBRARIES.md`.

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
| HOST-258 | test | Cargador HUNK: segmentos en `LinearArena`, relocaciones (32/32SHORT) y símbolos; detección `.englib`/HUNK. |
| 210_zone_resources | demo | Transición de zona: prefetch + LRU + overlay de código. |

## Riesgos y decisiones abiertas

## R6 — VFS, compresión y ownership de módulos

R0–R5 describen la base de E/S, caché y parsing, pero no cierran un VFS normalizado ni la carga de
librerías comprimidas con memoria elegida por segmento. El contrato completo está en
[`FILE_SYSTEM_AND_DYNAMIC_LIBRARIES.md`](../../engine/architecture/FILE_SYSTEM_AND_DYNAMIC_LIBRARIES.md).

- **R6.1 VFS**: normalizar paths, mounts, directorios, errores, cancelación y generaciones de
  requests; HOST de backend simulado. **✅ normalización de paths hecha**: `eng/os/path.hpp`
  (`eng::os::normalize_path` + `PathError`) colapsa separadores, resuelve `.`/`..` y rechaza
  escapes; **HOST-403**. ⏳ faltan la fachada `Vfs` (mounts/dispositivos, resolución relativa a una
  raíz, enumeración de directorios, handles propietarios) y los requests con generación (R6.2).
- **R6.2 Requests robustos**: separar `RequestId` del `IoUser`, conservar path y buffer hasta el
  fin, rechazar respuestas tardías y cerrar requests en vuelo. **✅ núcleo hecho**: `eng/os/request.hpp`
  (`RequestTable<MaxSlots>`/`RequestId` con **generación por slot**) — `acquire`/`alive`/`complete`/
  `cancel`, rechaza respuestas **tardías** de un slot reutilizado; **HOST-404**. ⏳ falta integrarlo en
  la E/S (validar la generación del `FileDone` antes de escribir) y conservar el path/buffer en el VFS.
- **R6.3 Política de memoria**: reservar código, datos y BSS por segmento con `MemoryManager`,
  respetar `HUNKF_CHIP`/`HUNKF_FAST`, fallback explícito y pools persistentes liberables. Usar
  `FastPreferred` automáticamente para segmentos CPU-only cuando haya Fast; Chip requerido nunca
  degrada a Fast. Ver `FAST_RAM_POLICY.md`. **✅ hecho (HUNK)**: `HunkImage::load(image,
  MemoryManager&, MemoryPolicy any = FastPreferred)` reserva **cada hunk en su banco**
  (`HUNKF_CHIP`→`ChipRequired` sin fallback, `HUNKF_FAST`→`FastRequired`, sin flag→`any_policy`),
  guarda el `Block` (banco efectivo en `block.kind`) y `unload(mem)` lo libera (`owns_memory`);
  **HOST-401** (Any→Fast/Slow, Chip→Chip, unload restaura). ⏳ falta aplicar la misma política al
  `DynLoader`/`.englib` (R6.6) y a la caché de assets.
- **R6.4 Contenedor comprimido**: crear `.engz` con codec, tamaño comprimido/descomprimido, alineación,
  política, CRC y payload HUNK/ENGL. **✅ contenedor hecho**: `eng/res/engz.hpp` (`build`/`parse`/
  `decode_engz`/`verify`) con codec, tamaños, alineación y **CRC-32** sobre el payload, compuesto
  sobre `res::decode`; **HOST-400** (construir→parsear→decodificar, corrupción → `Corrupt`, magic/
  truncado). El payload es un blob arbitrario (los formatos HUNK/ENGL van por su lado: `dynloader`).
  ⏳ falta integrarlo en la E/S asíncrona (leer de disco → decodificar → reservar por segmento).
- **R6.5 Decode ZX0 genérico**: reutilizar el depacker existente fuera de `eng::audio` como etapa
  de recursos y validar truncado, límites y CRC. **✅ hecho**: el depacker vive en
  `eng/res/zx0.hpp` (`eng::res::zx0`; `eng/audio/zx0.hpp` queda como alias `eng::audio::zx0`) y
  `eng/res/decode.hpp` (`eng::res::decode`) es la **etapa genérica** de recursos (`Codec::Raw`/
  `Codec::Zx0`), cubierta por **HOST-398** (Raw, ZX0 con el vector del compresor de referencia,
  codec desconocido y «no cabe»). Falta integrarla en el contenedor con CRC (R6.4).
- **R6.6 DynLoader propietario**: integrar lectura asíncrona, estados, imports/ABI, init/fini,
  refcount/pin, rollback y descarga segura. **✅ ownership hecho**: `DynLoader::load(h, image,
  MemoryManager&, policy)` **posee** la memoria (HUNK por banco vía R6.3; `.englib` copiado a un
  bloque) y `unload(h, mem)` la libera (por banco efectivo), con error sin fugas; **HOST-402**.
  ⏳ faltan lectura **asíncrona** (R6.2 + `os::file_*`), imports/ABI, `init`/`fini` y refcount/pin.
- **R6.7 Integración**: demo de transición de zona que cargue `.engz`, ejecute un export y descargue
  la librería sin bloquear el frame.

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

R0 está casi entregada; R1/R2 están entregadas; R3 es parcial; R4 está entregada como parser y
loader síncrono sobre una imagen ya disponible; R5 sigue pendiente. R6 queda abierta para cerrar el
VFS normalizado, el pipeline `.engz`/ZX0, la selección de banco por segmento y el ownership real de
las librerías. El diseño depende de la E/S asíncrona del mini-SO y de la política de memoria
persistente descrita en `MEMORY_OWNERSHIP.md`.
