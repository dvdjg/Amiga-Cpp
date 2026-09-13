# Contenido: tiles, mundo disperso, sprites y audio

Modelo de alto nivel de los **datos de contenido** con los que se construye una escena: dónde viven
los tiles, cómo se describe el mundo (que no es una matriz totalmente poblada), cómo se declaran
sprites/animaciones y sonidos, y cómo todo ello llega al display sin que la aplicación conozca el
hardware. Complementa `PUBLIC_API.md` y `SCENE_AND_RESOURCES.md`.

## 1. Motor de tiles

El motor de tiles es una **capa de contenido**: describe el mundo como una rejilla de referencias a
tiles, sin poseer píxeles ni framebuffer.

```text
Tileset        banco de tiles (píxeles) + paleta; profundidad 1..6 planos (incluye EHB).
TileId         índice en el Tileset (con variantes/glyphs si aplica).
TileSource     ACCESOR de tiles: `tile_at(x, y)` + `empty_tile`. El scroll consume esto.
```

- El algoritmo de scroll (XYLimited y variantes) consulta el `TileSource`, **no** una matriz
  concreta; sólo pinta los tiles distintos de `empty_tile` (los vacíos no gastan Blitter).
- El `Tileset` lo emite el pipeline de assets (cuantización + corte + EHB); ver
  `docs/demos/tile-pipeline/PIPELINE_TILES_EHB.md` y `tools/amiga-tiles/README.md`.
- Geometría (tamaño de tile, potencia de dos) entra como NTTP para evitar divisiones en el hot path.
- Implementación: `engine/include/eng/field/tile_source.hpp` (concepto + `SparseTileMap`) y
  `tile_map.hpp` (`TileLayerMap`). Test: `tests/host/025_tile_source`.

## 2. Mundo disperso

El mundo **no** es una matriz densa `ancho×alto`: la acción recorre unos pocos caminos y la mayoría
de posiciones no se visitan. El contenido se describe como una estructura **dispersa** que el
`TileSource` resuelve:

```text
WorldMap (disperso)
  ├─ Chunks      regiones de tiles (p. ej. 16x16) almacenadas SOLO si están pobladas.
  ├─ Índice      directorio de chunks residentes (búsqueda barata, sin hash en el hot path).
  └─ Meta        spawns, triggers, colisiones, transiciones (por chunk o globales).
```

- **Sparse por chunks**: se reserva memoria proporcional a lo poblado, no al área total. El
  `tile_at` resuelve chunk + offset; los chunks ausentes devuelven `empty_tile` (fondo).
- **Streaming**: los chunks se cargan/descargan al **cache de chunks residentes** (Chip RAM) bajo
  presupuesto, típicamente desde una `BackgroundQueue` (ver `BACKGROUND_TASKS.md`). El
  `SCENE_AND_RESOURCES` controla el hueco de Chip RAM.
- **Coste**: el scroll recorre la banda entrante y pinta sólo tiles poblados; los tramos vacíos se
  saltan sin tocar el Blitter. El índice de chunks debe ser O(1) por consulta (sin `%`/`/` caros;
  potencias de dos).
- El mundo puede ser **toroidal** (wrap) o **acotado**; el `TileSource` declara el modo.
- Implementación: `SparseTileMap<Chunk>` (`tile_source.hpp`) y `ChunkCache<ChunkSize,Capacity>`
  (`chunk_cache.hpp`, pool de Chip RAM del llamador + LRU). Tests: `tests/host/025_tile_source`,
  `tests/host/026_chunk_cache`.

### 2.1 Compatibilidad con Tiled (.tmx/.tsx)

El mundo disperso se define de forma compatible con el formato del editor **Tiled**:
- **Tilesets** (`firstgid`): `gid - firstgid` da el índice en el `Tileset`; los bits de flip de Tiled
  (29–31: horizontal/vertical/diagonal) se limpian antes de indexar.
- **Mapas infinitos por chunks**: los `<chunk x y width height>` de Tiled, con el mismo tamaño de
  chunk que `WorldMap`, se cargan tal cual; los chunks ausentes son `empty_tile`.
- **Capas** (`<layer>` Ground/Road/Water): cada capa es un `TileSource` de la escena; separan
  material.
- **Object layers** (`<objectgroup>`): spawns, triggers y colisiones van a los **metadatos del
  chunk** (`Meta`), no son tiles.
- El importador es `tools/ehb/parse-tmx.mjs` (CSV o `<tile>`, `--resolve-tsx`); se amplía a mapas
  infinitos y a la conversión de `gid` a índice de banco.
- Referencia del formato: `docs/guides/roadmap/TILED.md`.

## 3. Sprites y animaciones

El contenido declara **qué se ve**; la representación (sprite hardware / BOB / CPU / playfield) la
decide el engine (`SCENE_AND_RESOURCES.md` §2).

```text
SpriteSheet    píxeles de las celdas de un sprite + paleta; identidad de contenido.
Frame          una celda del sheet (x, y, w, h) + duración opcional.
Animation      secuencia de Frame + bucle/one-shot + eventos (disparar sonido, spawn).
ActorTemplate  Visual (Animation) + tamaño + anclaje + preferencia de representación.
```

- La animación avanza por **tiempo de juego fijo** (determinista), no por frame de vídeo.
- Un `Actor` de la escena instancia un `ActorTemplate`; el engine elige representación y la puede
  reasignar (sprite→BOB) al cambiar el presupuesto, sin tocar el contenido.
- El enemigo grande con scroll propio puede declararse con preferencia `Layer` (playfield de un
  DPF, patrón Jim Power).
- Implementación: `engine/include/eng/graphics/animation.hpp` (`Frame`/`Animation`/`SpriteSheet`).
  Test: `tests/host/027_animation`.

## 4. Audio

Sonido y música son **handles** a contenido cocinado; la reproducción la resuelve el mixer/música.

```text
Sound          muestra(s) + parámetros (volumen/loop) -> SampleBank.
Music          módulo (P61/PT) reproducido por el reproductor.
GameAudio      orquesta SampleBank + política de voces (cooldown, límite, ducking).
```

- La app dispara `play(id)` / `music.play(track)`; no conoce canales de Paula, prioridad ni mezcla.
- El presupuesto de audio (4 canales, mezcla) entra en el modelo de recursos de `SCENE_AND_RESOURCES`.
- Detalle: `AUDIO_MIXER.md`, `MUSIC_PLAYER.md`, `GAME_AUDIO.md`.

## 5. Assets cocinados

Todo el contenido de runtime viaja en el contenedor **UAF-R** (blobs tipados: paletas, bitplanes,
tiles, sprites, copper, malla, audio), generado en el host por los pipelines y cargado por offset
sin heap. Ver `docs/tools/UAF_PACK.md` y `engine/include/eng/assets/uaf.hpp`.

- El `TileSource`/`Tileset`/`SpriteSheet`/`Sound` son **vistas** sobre blobs UAF (`Span`), no
  copias.
- Los datos generados por pipelines (tiles/EHB, sprites, audio) van a `out/assets/` y el resultado
  canónico de texto a `docs/`/`artifacts/`; los binarios no se versionan.

## 6. Relación con el resto

- API pública (la app no ve hardware): `PUBLIC_API.md`.
- Escena retenida y ocupación de recursos: `SCENE_AND_RESOURCES.md`.
- Modelo de playfields/scroll/composición: `PLAYFIELD_SCROLL_ARCHITECTURE.md`.
- Efectos de Copper/Blitter/Sprites: `VISUAL_EFFECT_SPRITE_DESIGN.md`.
- Pipeline de tiles/EHB: `docs/demos/tile-pipeline/PIPELINE_TILES_EHB.md`,
  `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md`.
- Audio: `AUDIO_MIXER.md`, `MUSIC_PLAYER.md`, `GAME_AUDIO.md`.
