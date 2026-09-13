# Formato de mundo incrustable (chunks binarios)

Formato binario canónico para los mapas de tiles del engine: describe capas como
rejillas de índices de **banco** (no `gid` de Tiled), en **chunks** de tamaño fijo, de
modo que el runtime lo monte sin decodificar y tanto el caso denso como el disperso y el
streaming usen la misma estructura. Es el formato que consume `eng::field::SparseTileMap`
/ `StreamingWorldMap` / `TileMapView`.

Cierra el **paso 2** del roadmap de streaming (`docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`,
Fase 7). Cómo se leen los chunks desde almacenamiento: `docs/engine/architecture/STREAMING_LOADER.md`.

## 1. Principios

- **Big-endian** (nativo m68k): los lectores (`read_be16`) funcionan igual en host.
- **`gid` → índice de banco resuelto en el host**: el runtime no conoce Tiled ni `firstgid`.
- **Chunks de tamaño fijo potencia de dos** (`ChunkSize`), típicamente 16×16: la coordenada
  local es una máscara; el chunk es la unidad de presencia, de carga y de dirección.
- **Cero parseo en runtime**: el directorio de chunks es un array plano que se puede indexar
  por búsqueda binaria o montar tal cual como `SparseTileMap`.
- **Un chunk `WorldMap` dentro de UAF-R** (`docs/tools/UAF_PACK.md`): el mundo viaja con su
  paleta y su banco de tiles en el mismo blob, referenciados por índice de chunk.

## 2. Contenedor

El mundo es un chunk `WorldMap` (nuevo `eng::assets::ChunkType::WorldMap = 13`) de un blob
UAF-R. El blob contiene además:

- uno o más chunks `Tiles` (el/los banco(s) de tiles);
- uno o más chunks `Palette`.

El `WorldMap` referencia el banco y la paleta por **índice de chunk** del propio blob, no por
puntero. Los bancos de tiles se generan con el pipeline actual
(`docs/demos/tile-pipeline/PIPELINE_TILES_EHB.md`, `tools/ehb/emit-xlimited-bank.mjs`).

## 3. Payload del chunk `WorldMap`

```text
WorldHeader (16 B)
  u16 version          // 1
  u16 flags            // bit0 = payload comprimido; resto reservado a 0
  u8  chunk_log2       // 2 => chunks de 4x4; 4 => 16x16 (potencia de dos)
  u8  layer_count
  u16 tiles_chunk      // indice del chunk Tiles en el UAF-R
  u16 palette_chunk    // indice del chunk Palette en el UAF-R
  u32 reserved
  u16 reserved2

LayerDesc[layer_count] (32 B cada uno)
  u16 id               // id de capa (Ground=0, ...)
  u16 kind             // 0 = tiles
  u16 width            // ancho del mundo en celdas
  u16 height           // alto del mundo en celdas
  u16 wrap_x           // 0 = acotado; !=0 = periodo toroidal en X (celdas)
  u16 wrap_y           // 0 = acotado; !=0 = periodo toroidal en Y (celdas)
  u16 empty_tile       // centinela "no se pinta" (por defecto 0xFFFF)
  u16 meta_count
  u32 dir_off          // offset del array ChunkEntry (relativo al inicio del payload)
  u32 dir_count        // numero de chunks presentes
  u32 cells_off        // offset del bloque de celdas empaquetadas
  u32 meta_off         // 0 si no hay metadatos

ChunkEntry[dir_count] (4 B cada uno, ordenado por (cy,cx) ascendente)
  s16 cx               // indice de chunk (coordenada de chunk, no de celda)
  s16 cy

cells[dir_count * (1 << (2*chunk_log2))]   // u16 big-endian, en ORDEN de directorio

meta (opcional, por capa)
  MetaEntry[] { u16 type, u16 x, u16 y, u16 a, u16 b, u16 c }   // spawns/triggers/colision
```

Invariantes:

- `cells` va **en el mismo orden que el directorio**: el chunk de la entrada `i` ocupa
  `cells + i * chunk_cells`. No se almacena índice por entrada: la posición es el índice.
- `ChunkEntry` está **ordenado por `(cy,cx)`** para búsqueda binaria O(log n).
- Las celdas guardan **índices de banco**; el valor `empty_tile` (y los chunks ausentes) no se
  pintan. El pipeline **no** usa el índice 0 como "vacío": el vacío es `empty_tile`.
- Un mapa denso es el caso `dir_count == 1` con `(cx,cy) == (0,0)` y tanto ancho como alto
  menores o iguales al `ChunkSize` (o una rejilla de chunks consecutivos).

Diagrama de memoria:

```text
 payload
 +----------------------+  props del mundo
 | WorldHeader (16 B)   |
 +----------------------+
 | LayerDesc[0]         |
 | LayerDesc[1]         |  geometria y offsets por capa
 | ...                  |
 +----------------------+
 | ChunkEntry[]  (cy,cx)|  ordenado -> busqueda binaria
 +----------------------+
 | cells:[] u16         |  chunk i en cells + i*chunk_cells
 | ...                  |
 +----------------------+
 | meta[] (opcional)    |
 +----------------------+
```

## 4. Mapeo al runtime

| Caso | Tipo del engine | Cómo se monta |
|---|---|---|
| Denso (1 chunk) | `TileLayerMap` (dense) o `SparseTileMap<ChunkSize>` | `cells` apunta al primer chunk; sin directorio |
| Disperso residente | `SparseTileMap<ChunkSize>` | un `Chunk{cx,cy,Span}` por entrada, con `Span` a `cells + i*chunk_cells` |
| Streaming | `StreamingWorldMap<ChunkSize,Capacity>` | `Loader` = búsqueda binaria en el directorio + copia de `cells + i*chunk_cells` |

- El `Span<const Chunk>` de `SparseTileMap` se construye **una vez** al montar (O(dir_count)),
  sin decodificar: cada `Chunk::cells` es una vista al bloque ya empaquetado.
- En streaming, el `Loader` resuelve `(cx,cy)` con búsqueda binaria y copia
  `chunk_cells u16` a la celda del pool (ver `STREAMING_LOADER.md`).
- Los límites (`width`/`height`/`wrap_*`/`empty_tile`) alimentan el `TileMapView`
  (`engine/include/eng/field/tile_source.hpp`), que resuelve el wrap y el borde.
- El `chunk_log2` del formato debe coincidir con el `ChunkSize` del `TileMapView` (potencia de
  dos); el pipeline lo emite en la cabecera.

## 5. Pipeline host

Nueva tool `tools/ehb/pack-world.mjs` que **generaliza** `tools/ehb/gid-to-bank.mjs` (no la
duplica: absorbe su lógica y la extiende a chunks):

```text
  parse-tmx.mjs (tmx.json: finito o <chunk>; CSV/XML/base64)   tiles.json (slice-tiles)
            \                              /
             \                            /
              v                          v
        pack-world.mjs: gid -> indice de banco, troceo en chunks,
        ordenacion del directorio, celdas empaquetadas
                        |
        +---------------+----------------+
        |                                |
   world.bin (payload)           world.uafr (UAF-R: WorldMap + Tiles + Palette)
        |                                |
   incbin .MEMF_CHIP            servir por offset / StreamLoader
```

Reglas:

- El `gid` se convierte a índice de banco **en el host**; el valor "sin tile" (gid 0) se
  escribe como `empty_tile`.
- El troceo respeta `ChunkSize` (16 por defecto) y descarta chunks totalmente vacíos
  (no entran en el directorio).
- Se auto-valida el round-trip: reconstruir el mapa denso desde los chunks y compararlo con el
  mapa de entrada (assert 100 %), igual que la regla del pipeline de tiles.
- Salida C: `world.bin` + un `.h` con `extern "C" const unsigned char g_world[]` y
  `g_world_size` para `incbin`; la cabecera del `WorldMap` la valida el runtime.

## 6. Consumidor runtime y validación

- Vista `eng::assets::WorldView` (`engine/include/eng/assets/uaf.hpp`, `ChunkType::WorldMap = 13`)
  sobre el `Span<const u8>` del chunk: valida `version`, `chunk_log2` y que
  `dir_off`/`cells_off`/`meta_off` caigan dentro del payload (reutiliza `eng::assets::Reader`), y
  expone cabecera, descriptores de capa, `find_chunk` (búsqueda binaria), `cell`, `chunk_bytes`,
  `tile_at` (con wrap/borde) y `meta_entry`.
- Test host `tests/host/031_world_view`: fixture de `WorldMap` con varias capas, cruce de chunk,
  wrap, chunk ausente, metadatos y validación de bloques.
- Pendiente: adaptador `WorldView` → `SparseTileMap`/`TileMapView` (montaje directo) y el test de
  equivalencia en demo (mismo resultado que el mapa denso, paso 4 del roadmap).

## 7. Versionado y compresión

- `version` sube con cambios incompatibles; un runtime rechaza versiones que no entiende.
- `flags` bit0 reserva la compresión del payload. Valor por defecto `0` (sin comprimir); el
  formato debe funcionar sin depender de ella. Cuando se habilite, el camino runtime es el
  depacker ya presente en `support/depacker_doynax.s` y el host necesita un packer
  correspondiente (aún no existe).
- `flags` y los campos `reserved*` quedan a 0 hasta que se definan extensiones (p. ej. varias
  capas en chunks separados, LOD de tiles, metadatos enriquecidos).

## 8. Relación con el resto

- Motor de tiles y mundo disperso: `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §1/§2.
- Contenedor UAF-R: `docs/tools/UAF_PACK.md`, `engine/include/eng/assets/uaf.hpp`.
- Carga desde almacenamiento: `docs/engine/architecture/STREAMING_LOADER.md`.
- Pipeline de tiles/EHB: `docs/demos/tile-pipeline/PIPELINE_TILES_EHB.md`,
  `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md`.
- Importador Tiled: `tools/ehb/parse-tmx.mjs`, `docs/guides/roadmap/TILED.md`.
