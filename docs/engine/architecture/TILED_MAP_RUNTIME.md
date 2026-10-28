# Runtime de mapas Tiled (modelo objetivo)

Documento de diseño del **runtime de mapas de juego importados desde Tiled**: qué subconjunto del editor se soporta, cómo se cuece en host, qué formato consume el Amiga, qué contratos separan contenido de render y cuál es la **capa de intenciones** que usa un juego. Es el modelo de referencia para `eng::map`/`eng::Level`; el plan por fases y los gates están en [`ROADMAP_TILED_WORLD_SUPPORT.md`](../../guides/roadmap/ROADMAP_TILED_WORLD_SUPPORT.md). El subconjunto que entiende el importador actual y sus límites están en [`TILED.md`](../../guides/roadmap/TILED.md).

## 1. Alcance y niveles de soporte

El soporte se organiza en niveles acumulativos. Un juego declara el nivel que necesita; el cooker elimina del binario y de los assets lo que ese nivel no usa. La clasificación evita prometer «Tiled completo» en el A500 y permite entregar valor vertical desde el primer nivel.

| Nivel | Capacidad | Casos de uso | Estado |
|---|---|---|---|
| **N0 — Importación y validación** | Leer TMX/TSX/arte, producir un IR canónico, render de referencia y rechazos explícitos; sin runtime Amiga | Autoría, integración continua, informes de presupuesto | Objetivo de T0/T1 |
| **N1 — Mundo estático** | Ortogonal finito/infinito, una o varias capas estáticas cocidas en un fondo, cámara/scroll acotado o toroidal | Plataformas, shooters, CRPG con fondo fijo o scroll | Objetivo de T2/T3/T5 |
| **N2 — Capas vivas, animación y oclusión** | Capas `Overlay`/`Occluder`, tiles animados por Blitter, objetos tipados, colisión y actores | Plataformas con puentes/vegetación, CRPG con agua y objetos, shooters | Objetivo de T5/T6 |
| **N3 — Isométrico** | Proyección iso, orden por profundidad, picking, flips/rotaciones y sala cocida; scroll iso solo con presupuesto medido | CRPG/acción isométrica | Objetivo de T7 |
| **N4 — Mundos grandes** | Chunks lógicos y páginas de tiles en Chip con prefetch, pin, LRU y carga asíncrona | Mundos abiertos, mapas mayores que Chip RAM | Objetivo de T8 |

Géneros objetivo y nivel mínimo: plataformas laterales y *run-and-gun* → N1 + N2; *shooters* cenitales → N1 + N2; CRPG top-down → N1 + N2; CRPG/acción isométrica → N3; mundos grandes → N4. Un juego puede quedarse en N1 con un mapa cocido entero; el coste de los niveles superiores no se paga si no se solicita.

Fuera de alcance en este modelo: mapas hexagonales o *staggered*, *templates* con evaluación dinámica en runtime, mezcla alfa arbitraria por píxel, *tint* y opacidad de capa en runtime, y cualquier parseo de XML/PNG/TSX en el Amiga. La lista de rechazos y alternativas está en §11.

## 2. Decisiones canónicas

- **D1 — Tiled es formato de autoría, no formato de runtime.** El Amiga no parsea TMX, TSX ni PNG. Un *cooker* host convierte el proyecto a un bundle binario versionado; el runtime solo valida cabeceras y consume vistas. Esta decisión elimina XML, descompresión, cuantización y decodificación de imagen del camino de carga y hace determinista el contenido que llega a la máquina.
- **D2 — Una sola fuente de verdad y una sola tubería.** La fuente es el proyecto Tiled y sus imágenes. La salida va a `out/assets/tiled/<proyecto>/` como bundle UAF-R, manifiesto legible y informe de presupuesto. No se editan a mano intermedios ni se mantiene una segunda tubería de conversión. `png2amiga` se consulta como referencia de algoritmos de cuantización/layout, pero no se integra como runtime ni se duplica lo que ya cubre `tools/amiga-tiles`; si se porta una rutina, se respeta su licencia y se documenta la atribución.
- **D3 — IDs estables por encima de la residencia.** Las celdas guardan un `VisualId`/`TileRef` estable asignado por el cooker; nunca el slot de una página o de una caché LRU. La página puede cambiar de slot sin reescribir el mapa. La transformación (flip H/V/diagonal) es explícita y se materializa como variante precocinada o como operación de Blitter declarada, nunca se descarta en silencio.
- **D4 — Política de capas explícita y conservadora.** El cooker clasifica cada capa como `Baked`, `Base`, `Overlay`, `Occluder`, `Parallax` o `CollisionOnly`. Las capas estáticas se cuecen por defecto; solo se mantienen vivas las que necesitan animación, cambio de visibilidad, paralaje independiente u oclusión de actores. La política se puede forzar por propiedades de Tiled o por el `cook-plan`.
- **D5 — XYLimited es el presenter ortogonal; el isométrico es un presenter aparte.** La familia XYLimited (corkscrew 8-way, single/DPF, HUD) es la base para mapas ortogonales. Se amplía con una pila de capas de tiles, máscaras, parcheo de animación y un pase de oclusión después de actores. El isométrico no se fuerza dentro de XYLimited: reutiliza contenido, catálogo y cachés, pero tiene su propia proyección y orden de dibujo.
- **D6 — Composición barata por construcción.** Un fondo base opaco + capas `Overlay` enmascaradas + un pase `Occluder` después de actores es el modelo por defecto. El Blitter compone lo que cambia (banda entrante, parche animado, oclusión sucia); el DPF se usa para un segundo campo de paralaje cuando la paleta y la prioridad lo permiten. No se compone la pantalla completa por capas en cada frame.
- **D7 — Memoria DMA tipada por banco.** Todo lo que el hardware lee por DMA (páginas de tiles, máscaras, bitmaps de fondo, listas de Copper, sprites) viaja como `Block<Tag, MemoryKind::Chip>`/`ChipView<Tag>` desde la fuente; no se acepta un `Span` agnóstico en la API que llegue a DMA. El modelo sigue `INTERNAL_TYPE_SYSTEM.md` y las reglas de `AGENTS.md` §1.10.
- **D8 — Perfiles de compilación.** Un mapa residente que cabe se consume sin caché de chunks ni de páginas; un mapa con streaming activa solo los módulos necesarios. El perfil se elige al compilar o en el tipo del nivel, no con ramas por celda en el bucle caliente.

## 3. Pipeline host

```text
Proyecto Tiled                         Host (Node, sin runtime Amiga)
┌──────────────────────────┐
│ .tmx + .tsx + imágenes   │
└────────────┬─────────────┘
             │ parser estructural + validación
             ▼
      tiled-ir.json  (IR canónico versionado)
             │
             │ cook-plan.json (paleta, capas, chunks, animaciones, objetos)
             ▼
      cook-map.mjs
             │
     ┌───────┼───────────────┬──────────────────┐
     ▼       ▼               ▼                  ▼
  map.uaf  manifest.json  report.json      preview.png
  (UAF-R)  (nombres)      (presupuesto)    (referencia)
     │
     ▼
  incbin / Assets / streaming
```

**¿Carga directa en el Amiga?** No. Un TMX exige parser XML, resolución de TSX, decodificación de base64/compresión, lectura de PNG y cuantización de color; en un 68000 sin heap, con 512 KB de Chip y un presupuesto de frame de 50 Hz, ese trabajo no cabe ni es determinista. La alternativa «utility de conversión» es la adoptada: el proyecto Tiled sigue siendo la fuente de verdad y el cooker produce un bundle que el Amiga solo valida y consume. La puerta queda abierta a cargar **capas crudas ya cocinadas** (todas vivas) para prototipos, no a parsear Tiled en runtime. De `png2amiga` se evalúan/portan algoritmos concretos de cuantización, dithering, layouts interleaved y máscaras, con test de equivalencia y atribución; no se copia el proyecto entero ni se convierte en una segunda tubería.

### 3.1 Parser e IR

El extractor legacy (`tools/ehb/parse-tmx.mjs`) usa expresiones regulares: no respeta anidamiento, no lee capas de objetos, grupos, propiedades ni animaciones y limpia los flips. El parser estructural (`tools/tiled/parse-tmx.mjs`) está implementado (T1), es host-only y emite `eng.tiled-ir` v1; `tools/tiled/README.md` documenta el esquema y los fixtures. Cubre:

- XML con jerarquía y entidades; no regex. Parser propio con tests de conformidad; el motor no arrastra ninguna dependencia de parseo.
- `.tmx` y `.tsx` externos e inline; rutas relativas al fichero que las declara; múltiples tilesets; `firstgid`, `tilecount`, `columns`, `margin`, `spacing`, `tileoffset`, `objectalignment`.
- Codificaciones de mapa: CSV, XML `<tile>` y base64 con gzip/zlib. `zstd` se rechaza con mensaje explícito hasta que exista soporte host; no se intenta descomprimir a ciegas.
- Mapas finitos e infinitos por `<chunk>` con coordenadas negativas. Se conserva el **origen lógico** del mapa; el cooker no debe perderlo al normalizar a chunks de 16×16.
- Capas `tile`, `objectgroup`, `imagelayer` y `group` con `visible`, `opacity`, `offsetx/y`, `parallaxx/y`, `tintcolor`, propiedades y clases. Los offsets, opacidad, paralaje y visibilidad de un grupo se resuelven en el cooker al aplicar la jerarquía a sus capas hijas. Las propiedades desconocidas se preservan como datos opacos; las que el cooker no sepa interpretar no se aplican en silencio.
- GID: `id = gid & 0x1FFFFFFF`; flags H `0x80000000`, V `0x40000000`, D `0x20000000`. Se preservan y se validan combinaciones; no se enmascaran como parte del índice. Los bits de rotación hexagonal quedan fuera de alcance.
- Tilesets de atlas y de colección de imágenes. En colecciones, cada tile tiene su propia imagen y tamaño; no se asume una rejilla uniforme.
- Validación de tamaños: `width*height` o `chunk.width*chunk.height` frente a celdas decodificadas; longitudes base64 coherentes; índices de tileset dentro de rango (incluidos huecos de colecciones); objetos con `gid` resoluble. El rechazo de bounding rectangles extremos o imposibles de representar, y de spans que desborden `s32`, es un gate de T3 (cooker), no de este parser.

El IR (`eng.tiled-ir`, versión 1) es un JSON con `map`, `tilesets`, `layers`, `objects`, `properties` y un bloque `diagnostics`. No es un formato de runtime: puede ser verboso y conservar toda la información necesaria para cocinar, comparar y explicar un rechazo.

### 3.2 Cook plan

El `cook-plan.json` declara las decisiones que no se deducen del mapa o que el artista quiere forzar. Ejemplo para el fixture `Beginning Fields`:

```json
{
  "profile": "a500-ocs",
  "palette": { "mode": "global", "colors": 32 },
  "tile": { "w": 16, "h": 16, "page_tiles": 128, "mask": "auto" },
  "chunk": { "cells": 16 },
  "layers": {
    "Ground": "baked",
    "Road": "baked",
    "Shadows": "baked",
    "RockSlopes_Auto": "baked",
    "Water": "overlay",
    "Flowers": "overlay"
  },
  "objects": {
    "Object Layer 1": "decor",
    "Object Shadows": "bake"
  },
  "animations": { "mode": "patch", "max_visible_cells": 48 },
  "flips": "prebake-used",
  "opacity": "bake"
}
```

Las claves de `objects` usan la **política de objetos** (`bake`, `decor`, `actors`, `collision`), no el modo de una capa de tiles; un objectgroup no se convierte en `Base`/`Overlay` por sí mismo.

Reglas del cooker:

- **Paleta**: cuantizar todos los tiles que comparten playfield contra una misma tabla; no mezclar paletas incompatibles en un campo sin una regla de Copper/DPF. Elegir 16 colores (4 planos), 32 (5 planos), EHB (6 planos, 64) o DPF 3+3 según el perfil; medir la calidad con `tools/amiga-tiles/compare-palettes.mjs` y visión antes de fijar un mapa.
- **Tiles usados**: extraer solo los tiles referenciados por celdas y objetos, más las variantes de flip usadas y los frames de animación visibles. No volcar un atlas de 4096 tiles si el mapa usa 76.
- **Páginas**: agrupar tiles por página de tamaño fijo (p. ej. 64/128 tiles), con layout DMA (interleaved/planar) y máscara 1bpp cuando la capa sea `Overlay`/`Occluder`. Colocar juntos los frames de una misma animación para que el parcheo no provoque *page miss*.
- **Capas**: las capas invisibles se omiten salvo que el cook-plan pida conservarlas; las estáticas se funden en el fondo base en orden de profundidad y las que se declaren vivas conservan su `WorldMap`/`MapLayerCells` propio. Los `<chunk>` de Tiled se reagrupan al tamaño de chunk del bundle (potencia de dos, 16×16 por defecto) y el origen lógico se conserva. La opacidad y el *tint* de capa o grupo se hornean contra la capa inferior o se rechazan; el runtime no hace blending alfa general.
- **Objetos**: los *tile objects* de colecciones de imágenes se cuecen como sprites/BOBs con su rectángulo, anclaje y polígono de colisión; los rectángulos/polígonos de objectgroup se cuecen como triggers/colliders. Los objetos estáticos pueden hornearse en el fondo si el plan lo permite.
- **Animaciones**: cada tile animado se convierte en un grupo con frames (`VisualId`), duración en ticks de juego y política de actualización. El frame base sigue siendo una referencia estable; el animador resuelve el frame actual. Para una capa animada se recomienda **hornear las celdas estáticas en el base y dejar solo las celdas animadas como `Overlay` disperso**: el runtime parchea únicamente esas celdas. Cocer la capa completa por frames solo se contempla para animaciones pequeñas/cíclicas y se reporta su coste; el modo por defecto es el parche de celdas visibles por Blitter.
- **Flips**: se generan variantes precocinadas para las combinaciones usadas; se informa del coste. Para el resto se reserva una ruta de transformación en Blitter, no se ignoran.
- **Determinismo**: misma entrada + mismo plan ⇒ mismo bundle byte a byte. El cuantizador usa semilla fija y orden estable; el informe incluye hashes de entrada/salida y la versión del cooker.

### 3.3 Salidas y validación

- `map.uaf`: bundle UAF-R con los chunks de §4.
- `manifest.json`: nombres lógicos, ids de capa, paleta, tamaños, offsets y presupuesto; es lo que el juego referencia por nombre.
- `report.json`/`report.txt`: tiles únicos, páginas, bytes Chip/Fast, capas, objetos, animaciones, flips, avisos y rechazos.
- `preview.png`: reconstrucción determinista del estado estático para comparar con Tiled.
- `map.h` (opcional): `extern "C"` + tamaño para `INCBIN`, siguiendo el patrón de `pack-world.mjs`.

Todo se escribe en `out/assets/tiled/<proyecto>/` (generado, fuera de git); el repo conserva las fuentes y, si se decide, manifiestos de texto, nunca los binarios generados. La referencia visual se obtiene con `tmxrasterizer` del propio proyecto Tiled: render completo y renders por capa (`--show-layer`, `--hide-layer`, `--ignore-visibility`). La comparación es a 100 % de escala y por capa cuando sea posible; una diferencia de píxeles debe explicarse (paleta, opacidad horneada, recorte de objetos) o el cooker falla. El preview no sustituye la validación en Amiga: solo demuestra que el cooker no ha perdido contenido.

## 4. Formato cocinado: MapBundle

Estado: `MapBundle` v1 **baked** implementado (`tools/tiled/map_bundle.mjs`, `eng::map::MapAssetView`/`LayerView`, HOST-498): cabecera BE, una o más capas de celdas `u16`, paleta y banco externo. Catálogo `TileRef`, páginas y objetos siguen siendo objetivo de T4/T5.

El `WorldMap` actual (versión 1, una capa, un banco/paleta) se conserva para los mapas y capas que usan un **banco único residente** y para no romper a los consumidores existentes. Las capas que usan catálogo/páginas necesitan una variante con el mismo directorio y celdas pero referencias a `TileCatalog`/`TilePages`: `MapLayerCells` (o `WorldMap` v2, decisión de T3). Un `MapBundle` añade los chunks que faltan para describir un proyecto Tiled soportado. Los tipos nuevos se añaden a `eng::assets::ChunkType` cuando se implementen; los números concretos se fijan en T3 y no pueden colisionar con los existentes.

| Chunk | Contenido | Consumidor |
|---|---|---|
| `MapManifest` | versión, orientación, `renderorder`, tamaño de tile y de imagen, `chunk_log2`, origen lógico, flags, recuento y referencias a los demás chunks | `map::AssetView` |
| `MapLayers` | descriptor por capa: id, nombre, rol/modo, dimensiones, wrap, origen, offsets, paralaje, z, paleta, referencia a su `WorldMap` | `map::AssetView` |
| `WorldMap` v1 / `MapLayerCells` | directorio de chunks ordenado + celdas `u16` con `VisualId`; `0xFFFF` = vacío. v1 referencia un banco/paleta único (compatibilidad); `MapLayerCells` referencia el catálogo y las páginas | `WorldView`/`LayerCellSource` |
| `TileCatalog` | `VisualId` → `{tileset_id, local_tile_id, page_id, tile_in_page, transform, animation_group}` | `TileResolver` |
| `TilePages` | páginas de tiles en layout DMA + máscaras 1bpp + metadatos de alineación | `TilePageCache` |
| `TileAnimations` | grupos, frames (`VisualId`), duración en ticks, flags de bucle | `TileAnimationSource` |
| `MapObjects` | registros tipados de spawn/trigger/collider/path/camera/door/mechanism/decor, propiedades y referencias a colisión | `ObjectSource` |
| `Collision` | bitsets/materials por chunk, shapes de tile y proxies de objeto | `CollisionQuery` |
| `Palette` | una o dos paletas (PF1/PF2) y mapa de página→paleta | composición |
| `Tiles` | datos de tiles sueltos o de colección que no encajan en páginas fijas | `TilePageCache` |

Invariantes:

- Las celdas guardan `VisualId`, no slots. Un `VisualId` es único dentro del mapa y estable entre ejecuciones y recargas.
- `TileCatalog` es la única fuente de traducción de lógica a física; el renderer no interpreta `gid`, `firstgid` ni bits de Tiled.
- `TilePages` solo contiene datos que el Blitter puede leer directamente (Chip, layout y alineación declarados); las páginas de staging CPU no se publican como residentes.
- `MapObjects` y `Collision` están separados de las capas visuales: un tile puede cambiar de imagen sin cambiar su colisión, y una puerta puede cambiar de estado sin reescribir el mapa.
- `MapManifest` declara el perfil mínimo (residente/streaming, planos, paleta) y el runtime rechaza un bundle que no encaja antes de tocar memoria.

Para mapas que caben, el bundle puede ser un único blob residente. Para streaming, el manifiesto y el catálogo van en RAM rápida (Fast, o Slow si no hay Fast) y las páginas/chunks se sirven por offset desde disco o desde un blob mayor. La procedencia de bytes sigue siendo responsabilidad del `Loader` (`STREAMING_LOADER.md`).

### 4.1 Residencia y streaming

Mapa disperso y tileset grande son problemas distintos y usan cachés distintas:

```text
MapBundle
  ├─ celdas lógicas  ──► MapChunkCache (Fast/Slow; solo CPU) ──► LayerCellSource
  └─ VisualId        ──► TileCatalog (permanente, pequeño)
                              │
                              ▼
                       TilePageCache (Chip; slots fijos) ──► Blitter / bitplanes
```

- `ChunkCache`/`StreamingWorldMap` cachean **celdas lógicas**; no resuelven la residencia del bitmap del tileset en Chip.
- `TilePageCache` mantiene páginas **Chip** con slots fijos, generación de slot, `request/pending/ready`, pin por frame/fence y LRU. Solo una página lista y pinneada puede ser origen DMA de un `BlitJob`.
- El prefetch cubre la unión de páginas necesarias por la ventana visible, la guarda del scroll, las capas visibles, las animaciones activas y el overhang de objetos/arte, más un margen de varios frames. Se agrupan lecturas contiguas por página/sector.
- Una página referenciada por `FramePlan`, Blitter activo o pantalla frontal no se evicta hasta el fence y hasta salir de la ventana de prefetch. Una página expulsada puede volver a **otro** slot sin reescribir el mapa: las celdas guardan `VisualId`, no el slot.
- Si una página no está lista, el render usa placeholder/tile de error o retrasa la cámara/animación según política; `tile_at`/render no bloquea esperando E/S. El miss se registra en `stats()`.
- El tamaño de página debe equilibrar fragmentación Chip, lectura secuencial y el conjunto visible de la cámara; páginas demasiado pequeñas multiplican metadatos y misses. Los frames de una animación se agrupan en la misma página o en páginas contiguas.

Perfiles: `Resident` elimina `MapChunkCache` y `TilePageCache` del binario cuando el mapa y sus páginas caben; `ChunkStreaming` mantiene celdas lógicas en streaming con tiles residentes; `FullStreaming` activa ambos. El perfil se declara en `MapManifest` y se valida al abrir.

## 5. Modelo lógico

### 5.1 Referencias e identificadores

- `TileRef`/`VisualId`: `u16`, `0xFFFF` reservado para vacío. El cooker asigna un id por combinación única de `(tileset, tile local, transformación)` usada en el mapa; los frames de animación reciben sus propios `VisualId` y el grupo los referencia. Si un mapa necesita más de 65 535 combinaciones, se activa la variante `u32` del `WorldMap` y el catálogo lo declara; no se trunca.
- `LayerId`: `u16` estable dentro del bundle, independiente del orden de las capas en Tiled (el número de capas vivas simultáneas se limita por perfil, no por el id).
- `PageId`: identidad de página de tiles; el slot físico lo decide la caché.
- `AnimationId`: grupo de animación; los frames son `VisualId` y la duración está en ticks de juego, no en milisegundos de reloj de pared.
- `ObjectId`: `u32` estable del objeto de Tiled; los objetos pueden referenciar propiedades y un `collider_id`.

### 5.2 Capas

Cada capa lógica declara: `id`, `name`, `mode`, `role`, `width`, `height`, `wrap_x/y`, `origin_x/y` (en celdas), `offset_x/y` (px), `parallax_num/den` por eje, `z`, `palette_id`, `tileset_range`, `cell_source` y flags de visibilidad. El `mode` es la intención de composición, no una técnica concreta:

| Modo | Significado | Coste runtime | Mutabilidad |
|---|---|---|---|
| `Baked` | Fundida en el fondo base durante el cook | Cero (viaja en el base) | No; requiere recocinar |
| `Base` | Capa opaca viva que sirve de fondo del campo | Llenado de banda entrante | Visible/invisible; contenido editable |
| `Overlay` | Capa viva con transparencia, compuesta sobre el base al llenar la banda | Proporcional a sus tiles no vacíos en la banda | Visible, animada, editable |
| `Occluder` | Capa viva enmascarada dibujada **después** de los actores | Proporcional a las celdas sucias que se redibujan | Visible, animada; no se hornea |
| `Parallax` | Campo/cámara independiente (DPF o campo propio) | Un campo más; medir | Cámara/velocidad propia |
| `CollisionOnly` | Sin render; solo colisión/navegación | Consulta O(1) | Overrides de gameplay |

`Baked` y `Base` son excluyentes por campo: un campo necesita exactamente un fondo opaco. Las capas `Overlay`/`Occluder` se dibujan de abajo arriba; el orden lo fija `z`, no el orden de Tiled si el artista lo cambia. Un `Occluder` no puede ser a la vez `Baked`; si el plan lo pide, el cooker falla con un mensaje que nombra la capa. La edición en runtime se limita a capas `Base`/`Overlay`/`Occluder` con fuente mutable (p. ej. `TileEditor`/overrides); `Baked` no se edita. La edición marca dirty del chunk/capa y no cambia los IDs estables.

### 5.3 Animación de tiles

La celda guarda el `VisualId` base; el catálogo asocia ese id a un `AnimationId`. La duración en milisegundos de Tiled se convierte a ticks de juego con aritmética acumulada (no se redondea cada frame, para no derivar). El animador mantiene un reloj de juego determinista y resuelve `frame(animation_id, tick) -> VisualId`. El renderer dibuja el frame actual al llenar la banda y, si el frame cambia después, emite un parche para las celdas visibles afectadas. El cooker emite por chunk una lista dispersa de celdas animadas (`x, y, group`) para que el runtime no recorra la capa completa. Los frames de un grupo se colocan en la misma página o en páginas contiguas para evitar *page miss*; el informe declara cuántos frames y bytes añade cada animación.

### 5.4 Fondo de celdas vivas

El mapa cocinado acelera el scroll, pero **no es la fuente del fondo de una celda viva**. El fondo real se guarda en el cache del surface y se reutiliza mientras la celda siga visible:

- `MapCellSaveCache<Capacity>`/`MapCellSaveCacheSet<Small, Large>` (`engine/include/eng/graphics/map_sprite.hpp`) capturan el destino antes de pintar el overlay y guardan la captura por celda/grupo y posición del anillo, no por índice de tile;
- la memoria son **arenas Chip fijas**: 96 × 192 B = 18 KB para celdas de tile y 8 × 1,5 KB = 12 KB para sprites fusionados en 207; no hay una reserva por celda y el `BlockPool` no se fragmenta;
- la captura se conserva: al cambiar el frame se emite `restore` (`RestoreRect`) y se vuelve a pintar; solo se captura una vez. Si la celda sale de la ventana, `release` libera la entrada sin restaurar (el scroll repinta esa columna al entrar);
- nunca se reconstruye el fondo con `add_draw` ni con el tile del `Baked`, porque el bake puede no contener el píxel real bajo la celda;
- el overlay y el save-under comparten el direccionamiento del anillo: `target_x = wx` (columna de mundo sin envolver, la misma que el bloque base), `ring_y` del anillo y paso entre planos `row_bytes`; `sx`/`sy` sólo deciden visibilidad;
- las celdas de un objeto compuesto se actualizan en el mismo pase. El parche recorre las capas de primer plano a fondo y respeta el tope de jobs declarado en `plan.blit_budget_limits().max_jobs`; la fogata de 207 es un único grupo fusionado de 32×32 (1 celda) que cabe en 24 jobs. Las capas de tiles independientes se reparten con el cursor round-robin.
El contrato de memoria (arenas fijas, captura única y liberación al salir) pertenece al cache del surface; el render sólo pide `capture`/`restore`/`release` para las celdas visibles.

#### 5.4.1 Objetos libres y caché precompuesta: qué aplica y qué no

La ruta de 207 es **celda alineada a tile** con una excepción automática: `Water` (161 celdas) y `Flowers` (62) siguen como grupos de celdas de 16×16 (`TileAnimator`); la capa `Object Layer 1` contiene la fogata 2×2, y el cooker la fusiona en **un grupo de 32×32** (1 celda, un frame compuesto por frame). Cada celda/grupo usa save-under del fondo real, `restore` antes del frame nuevo y un blit enmascarado. La decisión de fusión no conoce la fogata: es una regla del cooker sobre el mapa.

**Regla de fusión automática** (`cook-animations.mjs`): un cluster de celdas animadas se cocina como un sprite si (1) están en la misma capa y son contiguas; (2) su bounding box está completamente ocupado (sin huecos); (3) todas comparten la misma línea temporal de frames (mismo número y duraciones); (4) hay al menos 2 celdas y 2 gids distintos; si un gid se repite en el mapa, la fusión duplica frames y solo se acepta si `animations.fusion.max_duplicate_bytes` cubre el coste (por defecto 0, que exige gids únicos); (5) el cluster no supera `maxCells` (8). El grupo resultante declara `pixel_width/pixel_height` y su propio `sheet_offset/frame_stride`; el runtime lo dibuja con **un solo blit por segmento** y un save-under de su caja exacta. El `MapCellSaveCacheSet` elige la arena pequeña (tiles) o la grande (sprite fusionado).

Las ideas de **map sprite libre** (rectángulo no alineado, background+frame cocinados, bounding boxes dinámicos, minimizar cookie-cut, blit del mayor tamaño posible) se comparan así:

| Idea | Estado actual | Veredicto |
|---|---|---|
| Sprite no alineado a tile | La fusión automática cubre clusters **alineados** a la rejilla; un objeto de colección a posición arbitraria sigue pendiente | **Necesario para T5/T6**: un sprite no alineado no se representa con celdas sin recortar. La base existe: `MapSprite`/`MapSpriteCache` colocan un `Sprite` de tamaño libre con `place_map_sprite` y save-under por instancia; falta que el cooker emita `MapObjects` con rect en píxeles y frames, y que el render los ordene |
| Pre cocinar **sólo el fondo** de cada objeto | Se captura el **destino real** una vez (`MapCellSaveCacheSet`); nunca se reconstruye del mapa | **No ahorra jobs** frente al save-under: copiar el fondo + dibujar el sprite enmascarado son los mismos 2 trabajos que `restore` + cookie-cut. Sólo es válido si el contrato declara que bajo el objeto no pasa nada dinámico; si algo lo tapa, el fondo cocinado queda obsoleto. El save-under real es más general y cuesta lo mismo en cada frame |
| Caché background+frame por celda/frame | 96 capturas × 192 B = 18 KB de arena pequeña + 8 × 1,5 KB de arena grande, una por celda/grupo visible y reutilizada entre frames | Para **flores/agua no compensa**: 62×8×192 ≈ 95 KB y 161×8×192 ≈ 247 KB en Chip, y el fondo puede quedar obsoleto si otra capa lo tapa. La **fusión de frames** (no de fondo) sí se usa para objetos compuestos únicos, como la fogata |
| Un blit grande en vez de celdas | Implementado para clusters válidos: la fogata pasa de 4 restore + 4 draws a 1 restore + 1 draw por cambio | **Resuelto para objetos alineados**: un grupo de 32×32 cocinado como un solo frame/máscara reduce jobs; `TileAnimator` acepta tamaño por grupo y `MapCellSaveCacheSet` tiene arena grande. Para objetos mayores o no alineados, `MapSpriteCache` reserva el tamaño exacto |
| Bounding boxes dinámicos / orden por solape | Orden por capa y grupo; sin BVH | Para la fogata no aporta. Para actores/objetos libres, orden estable por capa y `y` (foot point) y `force` en los cruces; un BVH sólo se justifica con decenas/cientes de actores móviles |
| Minimizar cookie-cut | Máscara 1bpp por grupo; el coste es el área de la caja × planos, no los píxeles opacos | Partir un objeto en celdas **no reduce** el área enmascarada y multiplica jobs. El blit mayor es mejor para objetos grandes; para tiles de 16×16 la máscara ya es mínima |

**Conclusión**: la implementación actual es superior para el caso real de 207 (fondo compuesto, memoria acotada, sin recocinar cada frame) y la fusión automática ya aplica la idea del blit mayor a clusters alineados sin reglas específicas de la fogata. No debe añadirse una caché background+frame de flores/agua sin medidas que demuestren que el Blitter es el cuello y que hay Chip libre. Las mejoras con más valor son: (1) objeto libre no alineado con `MapSpriteCache` y `MapObjects`; (2) ampliar el umbral de fusión/arena grande para objetos mayores; (3) caché precompuesta **sólo** para objetos pequeños y estáticos sobre fondo inmutable, con invalidación explícita. Cualquiera debe medirse con `measure-fps` y el contador de jobs.

### 5.5 Objetos, colisión y coordenadas

Los objetos se guardan con posición en píxeles de mundo (`s32`), tamaño, `gid`/`VisualId` si es *tile object*, anclaje (`objectalignment`), offsets, flips y referencias a propiedades. Para *tile objects* de colecciones, el visual es un sprite de tamaño variable, no un tile de 16×16. La **política de objetos** del cook-plan decide su materialización: `Bake` (fundido estático en el base), `Decor` (objeto estático de mundo dibujado sobre el base y antes de los actores, sin orden por profundidad de actor), `Actors` (spawn con orden por foot point, profundidad y occlusión) o `Collision` (solo metadata). Los tile objects con animación emiten sus frames contiguos y una tabla `MapObjectAnimationView`; `eng::map::map_object_sprite` construye el `Sprite` planar y `MapSprite`/`MapSpriteCache` lo coloca en cualquier píxel de mundo con su save-under. La colisión se guarda aparte:

- Bitset de sólido por celda y capa de colisión.
- Material/flags por celda (agua, hielo, daño, one-way).
- Shapes de tile (polígonos de `objectgroup` del TSX) referenciadas por id; se usan para pendientes y colisión fina, no para cada celda.
- Proxies de objeto (polígonos de edificios, árboles) referenciados por `ObjectId`.

Los mecanismos (puertas, plataformas, interruptores, cofres) se representan como objetos/triggers con estado de gameplay; el runtime de juego los instancia y actualiza, y el mapa solo aporta su definición y collider. Espacios de coordenadas: **celda lógica** (mapa, colisión, objetos), **píxel de mundo** (cámara, actores) y **píxel de pantalla** (presenter). La proyección isométrica es una transformación de presentación/picking, no un cambio del espacio de colisión. El runtime no mezcla los tres.

## 6. Contratos internos normalizados

La separación de módulos se cierra con contratos pequeños. Los nombres son objetivo; las firmas exactas se ajustan al implementar, pero el **reparto de responsabilidades y los invariantes** son canónicos. Las capacidades son parámetros de plantilla o configuración de setup, nunca heap.

| Contrato | Responsabilidad | Implementaciones previstas | Invariantes |
|---|---|---|---|
| `map::AssetView` | Validar y exponer el `MapBundle` sin poseerlo | Vista sobre UAF-R residente o por offsets | No reserva, no parsea XML, rechaza versión/rangos inválidos |
| `LayerCellSource` | Devolver la celda lógica de una capa | Residente, dispersa, streaming, editable | Acceso O(1) o O(log chunks); nunca dispara E/S desde el render |
| `TileResolver` | Traducir `VisualId` → página/tile/transformación | Catálogo + tabla de páginas | IDs estables; una página ausente no cambia el id |
| `TilePageProvider` | Residencia y pin de páginas Chip | Caché LRU con slots fijos, residente | Solo se entrega DMA de páginas listas y pinneadas; pin por frame/plan |
| `TileLayerSink` | Dibujar una capa en una banda destino | XYLimited, strip scroller, canvas, iso | Escribe solo la ventana/clip; respeta máscara y alineación; no conoce la cámara |
| `LayerCompositor` | Ordenar y combinar capas de un campo | Base + overlays + occluders | Un solo base opaco; overlays de abajo arriba; oclusores tras actores |
| `ObjectSource` | Entregar objetos tipados y propiedades | Bundle residente/streaming | Sin XML; posiciones y anclajes ya resueltos |
| `CollisionQuery` | Responder consultas de gameplay | Bitset/material + shapes | O(1) por celda; independiente del tile visual |
| `TileAnimationSource` | Resolver frames por tick | Tabla de animaciones | Determinista; duración en ticks; sin asignación por frame |
| `MapPresenter` | Conducir el dibujo de un nivel en un backend | XYLimited, iso room, strip | No conoce Tiled; recibe cámara, tick y presupuesto; expone vistas de banda |

Forma ilustrativa de los contratos de contenido (los conceptos reales pueden usar `Expected`/`Ref` del engine):

```cpp
template <class T>
concept LayerCellSource = requires(const T& s, LayerId layer, eng::s32 x, eng::s32 y) {
    s.width(layer);
    s.height(layer);
    s.empty(layer);
    s.cell(layer, x, y);       // -> TileRef
};

template <class T>
concept TileResolver = requires(const T& r, TileRef ref) {
    r.resolve(ref);            // -> TileLocation { page, slot, tile, transform }
    r.page_ready(ref);         // -> bool
};

template <class T>
concept TilePageProvider = requires(T& p, PageId page, FrameTicket ticket) {
    p.request(page);           // -> PageRequest
    p.pin(page, ticket);       // -> bool
    p.unpin(page, ticket);
    p.view(page);              // -> ChipView<TilePageTag>
};

template <class T>
concept MapPresenter = requires(T& p, Backend& b, const Camera2D& cam, eng::u32 now) {
    p.begin(b);
    p.frame(b, cam, now);
    p.after_actors(b, cam);    // pase de occlusión, puede ser no-op
    p.band_view_count();
};
```

Reglas de contrato:

- **Nada de I/O en el render.** Un `page_ready == false` se resuelve con placeholder, retraso de cámara o mantener la página anterior; nunca bloquea el frame.
- **Pin explícito.** Una página referenciada por un `FramePlan`, un Blitter en curso o la pantalla frontal no se desaloja. El `TilePageProvider` recibe el ticket/fence y lo valida.
- **Sin slots en los datos.** El `TileCatalog`/`LayerCellSource` no guardan el slot LRU; el resolver actualiza la residencia y el render solo emite trabajos para páginas pinneadas.
- **Sin formas parciales.** Un `TilePage` no se publica sin layout, profundidad, paleta y máscara (si aplica); una capa no se dibuja sin `width/height/empty` y sin política de composición. Si el hardware exige N cosas acopladas, la firma las pide todas.
- **Tipos de banco.** Las vistas que terminan en DMA llevan `Chip` en el tipo; un `Span` sin banco no cruza la frontera de render.

## 7. API de intenciones: `eng::Level`

`eng::Level` es la **puerta principal** para un juego. Agrupa la carga, la consulta y la gestión del mapa; el juego no nombra `WorldView`, `TileCatalog`, `TilePageCache`, `BlitJob` ni geometrías de anillo. Por debajo compone `map::Runtime` (contenido) y un `MapPresenter` (render), que son los módulos internos normalizados de §6.

### 7.1 Ejemplo de uso

```cpp
using Level = eng::Level<eng::amiga::AmigaBackend, 320u, 256u, 4u>;

Level level;

void Game::init(eng::App& app) {
    const auto map = app.assets().map("beginning_fields_32c");
    const auto opened = level.open(map, {
        .animated_cells = 48u,
        .objects = eng::ObjectPolicy::Spawn,
        .prefetch = true,
    });
    if (!opened) {
        return; // el juego consulta opened.error() para diagnóstico
    }

    level.camera().reset({0, 0, 640, 640}, {320u, 256u});
    const auto rocks = level.find_layer("RockSlopes");
    if (rocks) level.set_layer_visible(rocks, false);
    level.animations().set_enabled(true);
    app.add_level(level); // objetivo: arranca y conduce frame + pase after_actors
}

void Game::update(eng::App& app, eng::u32 now) { // `now` = tick de juego acumulado
    level.camera().move_by(input.dx(), input.dy());
    level.update(now);
    if (level.solid_at(player_cell_x, player_cell_y)) {
        // consulta de gameplay; no mira el tile visual
    }
}
```

La versión actual de la fachada ofrece `App::add_scroll_layer`; `App::add_level` es el nombre objetivo para el mismo contrato, con el pase de oclusión y la animación incluidos. Mientras no exista, un nivel puede registrarse como `ScrollLayer` y el juego conduce la cámara igual.

### 7.2 Superficie de intención

| Intención | Llamada objetivo | Semántica |
|---|---|---|
| Abrir/cerrar | `open(asset, options)`, `close()` | Valida el bundle, liga catálogo/páginas, prepara la presentación; error de setup si no encaja |
| Cámara | `camera()` | `Camera2D` acotada o toroidal; el juego mueve la vista, no los punteros |
| Capa | `find_layer(name)`, `set_layer_visible`, `set_layer_parallax` | Identidad estable por nombre; la visibilidad no recalcula el mapa |
| Modo de capa | `available_modes(layer)`, `set_layer_mode(layer, mode)` | Solo modos cocidos; un cambio no representable devuelve `false` y explica el motivo |
| Animación | `animations().set_enabled`, `set_speed` | Reloj de juego determinista; presupuesto de celdas visible |
| Celdas | `tile_at(layer, x, y)`, `cell(layer, x, y)` | Consulta lógica; devuelve `TileRef`, no un puntero a página |
| Edición | `set_tile(layer, x, y, tile)` | Solo capas mutables (`Base`/`Overlay`/`Occluder`); marca dirty, no reescribe IDs ni el bundle |
| Colisión | `solid_at`, `material_at`, `shape_at` | Consulta O(1) por celda; independiente de la capa visual |
| Objetos | `objects(kind)`, `spawn_objects(store)` | Spawns/triggers/colliders tipados; el juego decide qué instanciar |
| Presupuesto | `set_budget`, `stats()` | Páginas residentes, pinneadas, misses, celdas animadas, bytes; sin heap |
| Precarga | `prefetch(camera)` | Opcional; el `App` puede hacerla por frame con la cámara de la capa |

### 7.3 Semántica de errores y degradación

- `open` falla antes de tocar el display si el bundle no es válido, la paleta no encaja o el perfil de compilación no soporta el modo de capa pedido.
- Una página que falta en runtime no es un error fatal: se usa placeholder/tile de error o se retrasa la cámara, y `stats()` lo registra. El render nunca espera E/S.
- Un modo de capa no disponible se rechaza; no se convierte silenciosamente en `Baked` (perdería la oclusión o la animación). El cooker debe emitir el modo pedido o el juego debe elegir otro asset cocido.
- El juego puede quedarse en un subconjunto: `Level` no obliga a usar objetos, colisión ni animación; los módulos no usados se eliminan del perfil.

## 8. Render en A500

### 8.1 Modelo de composición

```text
                 campo visible (un playfield)
┌────────────────────────────────────────────────────────────┐
│ 1. Base opaco (Baked o Base viva)                          │
│ 2. Overlays vivos (tiles con máscara, de abajo arriba)      │
│ 3. Actores (BOBs/sprites; save-under del engine)            │
│ 4. Occluders (tiles con máscara, solo zonas sucias)         │
│ 5. HUD/overlay de pantalla (Canvas/Copper, capa aparte)     │
└────────────────────────────────────────────────────────────┘
```

El scroll llena la **banda entrante** del anillo con la base y los overlays; no recorre la pantalla completa. Los occluders se dibujan **después** de los actores, porque su semántica es tapar al jugador (puente, copa de árbol, alero). Un puente es un `Occluder`: el personaje se dibuja antes y el puente lo tapa; al salir, el save-under del actor y el redibujado de las celdas de occlusión sucias restauran el orden. El HUD no forma parte del campo: es otra capa (`CanvasPlayfield`/overlay) y no reduce el anillo del corkscrew (invariante de `XYLIMITED_ALGORITMO_GENERICO.md` §1.2).

Selección de variante por caso:

| Caso Tiled | Variante de render | Notas |
|---|---|---|
| Ortho N1 estático, scroll X/Y | XYLimited single (o `TileScroll`/tiras para X de 50 fps) | Base cocida, sin overlays |
| Ortho N2 con agua/flores/objetos | XYLimited single + `Overlay` + `Occluder` | Máscaras y pase `after_actors` |
| Paralaje de fondo | XYLimited DPF 3+3 o campo propio | Paleta y prioridad limitadas |
| Iso de sala | `IsoPresenter` de sala cocida | No usa el corkscrew rectangular |
| Iso con scroll continuo | `IsoPresenter` con anillo/proyección (N3b) | Pendiente de presupuesto medido |

### 8.2 Extensiones de la familia XYLimited

La infraestructura actual (`XlimitedScene`, `XlimitedPlayfield`, `XlimitedScrollLayer` para el corkscrew; `TileScroll`/`StripScrollLayer` para tiras) sirve para un mapa por campo, DPF, lienzo FG y HUD. Para Tiled se amplía sin romper los usos actuales:

1. **Pila de capas de tiles.** El campo compone N `LayerCellSource` (una por capa) y un `TileLayerStack`/`LayerCompositor` decide, por bloque entrante, qué capas se dibujan y en qué orden. La base se copia opaca; los overlays se dibujan con máscara. El contrato del `ScrollEngine` se mantiene: el motor pide bloques y el compositor decide qué tiles componen cada bloque.
2. **Máscaras de tile.** El cooker genera una máscara 1bpp para los tiles con transparencia (índice 0). El Blitter usa un minterm cookie-cut (`A AND B OR ~A AND C`) con A=máscara, B=tile, C=destino. Sin máscara, un overlay no puede ser correcto; no se acepta “tile transparente por convención” en el camino caliente.
3. **Pase de animación.** `TileAnimator` mantiene el tick y una lista de celdas animadas visibles por capa. Al cambiar un frame, emite parches enmascarados para esas celdas; si la celda entra en la banda, se pinta ya con el frame actual y no se duplica el trabajo. El presupuesto (`max_visible_cells`) se declara y se mide.
4. **Pase `after_actors`.** El presenter expone un hook que el `App` llama después de los actores y antes del flip. Dibuja solo las celdas `Occluder` visibles que intersectan las zonas sucias de actores/cámara/animación; no redibuja la capa entera. Un `Occluder` estático no necesita un `WorldMap` completo: el cooker puede emitir parches por chunk (rectángulo + máscara + tile) y el runtime redibuja solo los que intersectan dirty rects.
5. **Modos de capa y visibilidad.** `Baked` no se puede “deshornear” en runtime. `Base`/`Overlay`/`Occluder` admiten visibilidad y animación; el cambio de modo solo se acepta si el bundle contiene los datos del modo destino. El API no promete una transición que el asset no soporta.
6. **Contrato de cocción y prioridad.** El detalle operativo vive en [`TILED_COOK_PLAN.md`](TILED_COOK_PLAN.md): prefijo cocido (`baked_through`), modos por capa, fallback sin ghosting y orden de degradación (`Water` → `Flowers` → fuego) para preservar 50 fps. La base limpia + frame actual/congelado por celda viva evita necesitar un segundo bitmap completo.

7. **DPF y paralaje.** Un `Parallax` puede ser el segundo campo de un DPF (3+3) o un campo propio si la geometría lo permite; no se inventan capas independientes ilimitadas en OCS. La composición resuelve `BPLCON1`, prioridad, split y paletas compartidas como en `PLAYFIELD_SCROLL_ARCHITECTURE.md`.

### 8.3 Isométrico

El isométrico se trata como un **presenter distinto**, no como un modo de XYLimited. La primera entrega es **sala cocida** (N3a), que es la que mejor encaja en A500 y en CRPG:

- El cooker recibe las celdas lógicas y el orden de dibujo (`renderorder`), proyecta y **hornea** las capas estáticas de la sala en uno o varios bitmaps de pantalla (o bandas) con la paleta elegida.
- Los actores se ordenan por `u+v`, elevación/sublayer y un desempate estable (id); el anclaje visual es el *foot point*, no la esquina del bitmap.
- Los occluders iso (copas, tejados, arcos) se guardan como máscaras/BOBs de primer plano y se dibujan después de los actores.
- El picking convierte pantalla→celda con la proyección inversa y valida el rombo; se prueba con golden images.
- La cámara puede ser fija o limitada; el scroll iso continuo (N3b) se pospone a un presenter con anillo/proyección propia y presupuesto medido. No se fuerza el corkscrew rectangular sobre un mapa iso.

La proyección, el picking y el orden de profundidad siguen `PIXEL_ART_2D_ISOMETRIC.md` §3/§8. La colisión se resuelve en coordenadas lógicas `(u,v)` (y elevación si existe), nunca en la AABB proyectada.

### 8.4 Paleta y presupuesto de color

- **4 planos / 16 colores**: mínima huella, válida para N1/N2 con arte limitado; medir la pérdida con el informe de paleta.
- **5 planos / 32 colores**: máxima calidad en un solo playfield OCS sin EHB; opción recomendada para CRPG top-down y mapas con muchos materiales.
- **EHB (6 planos, 32+32)**: buena para fondos ricos y demos tipo 201; el sexto plano y el DMA se pagan.
- **DPF 3+3**: dos campos de 8 colores para paralaje/fondo vs primer plano; no es un compositor general de N capas y la prioridad limita las combinaciones.
- La paleta se elige por **campo**, no por tile. Un cambio de paleta por Copper solo se admite si el presupuesto y las zonas están declarados; si no, el cooker rechaza la mezcla.

## 9. Presupuesto y rendimiento

Fórmulas de trabajo (bytes, sin contar cabeceras ni alineación):

- Fondo/ventana: `row_bytes = width / 8`; `plane_bytes = row_bytes * height`; `frame_bytes = plane_bytes * planes`; doble buffer = `2 * frame_bytes`; el anillo vertical añade `2 * tile_h` líneas y la guarda X añade palabras.
- Tile: `tile_bytes = (tile_w / 8) * tile_h * planes`; con máscara, `+ (tile_w / 8) * tile_h` (1bpp).
- Página: `page_bytes = page_tiles * tile_bytes`; con máscara, `page_tiles * (tile_bytes + mask_bytes)`.
- Mapa: `cells * 2` bytes por capa (u16); un chunk de 16×16 ocupa 512 B. Los chunks ausentes no reservan.
- Animación: `frames * tile_bytes` por grupo; el parche por frame mueve `celdas_visibles * tile_bytes` de bytes de Blitter (medir, no estimar).
- Oclusión: el coste es el área sucia × planos; una capa `Occluder` completa redibujada cada frame es un antipatrón.

Ejemplo calculado con el fixture `Beginning Fields` (40×40 celdas de 16 px, 7 capas de tiles, 18 tilesets, 169 objetos):

| Elemento | Cálculo | Bytes aproximados |
|---|---|---|
| Mapa cocido como base | 40×40 celdas u16 | 3 200 B |
| Tiles únicos de capas | 198 ids + frames de animación; a 16×16×4 planos = 128 B/tile | ~32–48 KB |
| Objetos de colección | 58 imágenes únicas, ~63 KB en 4 planos (sin máscara) | ~63–80 KB |
| Fondo 320×256×4 | 40 960 B por buffer | 40 960 B |
| Anillo con guarda 352×288×4 | 50 688 B por buffer | 50 688 B |
| Doble buffer del anillo | 2 × 50 688 B | 101 376 B |

El total ronda **200–250 KB de Chip** para una versión residente con un solo campo, antes de HUD, sprites, audio y sistema. Es una envolvente de planificación, no una garantía: el informe del cooker y la reserva real del backend son la fuente. La versión EHB o 5 planos sube el fondo y los tiles; la versión con objetos como actores separados no los duplica si se comparten bancos/sprites.

Caps iniciales propuestos (configurables por perfil, no constantes universales):

- Como máximo **un `Base` por campo**; `Overlay`+`Occluder` vivos ≤ 3 por campo en A500, con presupuesto de banda medido.
- `max_visible_cells` de animación ≤ 48–64 por campo en el primer perfil; por encima, el cooker debe agrupar o el juego debe reducir la ventana. El valor definitivo se fija midiendo las celdas animadas visibles del fixture (el agua de `Beginning Fields` puede requerir más presupuesto que la cifra inicial).
- Superficie de oclusión redibujada por frame ≤ la unión de rects sucios, no la pantalla completa.
- Páginas residentes y pinneadas dentro del presupuesto Chip declarado; el `TilePageCache` devuelve `OutOfMemory`/miss, no roba memoria al display.
- Cada demo debe medir `measure-fps` y perfil `.amigaprofile`; una estimación de bytes no es evidencia de 50 fps.

## 10. Validación

### 10.1 Fixtures

- **`Beginning Fields`** (ortogonal, finito, 40×40, 7 capas de tiles, 2 objectgroups, 18 tilesets, animaciones de agua/flores/campfire, polígonos de colisión por tile). Es el fixture principal de N1/N2; el PNG `Beginning Fields.png` de 640×640 es un **render plano del mapa**, no un atlas de tiles, y sirve como referencia visual, no como fuente de tiles.
- **Puente sintético**: mapa pequeño ortogonal con una capa `Occluder` (tablones) sobre una base y un actor que pasa por debajo; sirve para el gate de oclusión y para medir el pase `after_actors`.
- **Iso de sala**: mapa isométrico pequeño con objetos altos y picking; sirve para N3a.
- **Infinito/negativo**: mapa con chunks en coordenadas negativas; valida origen lógico y floor division.
- **Flips y colecciones**: tiles con H/V/D y un tileset de colección; valida catálogo, variantes y anclajes.
- **Rechazos**: zstd, hex/staggered, opacidad no horneable, paleta incompatible, GID fuera de rango; cada uno con diagnóstico que nombra fichero/capa/objeto.

Los fixtures binarios pequeños se generan de forma determinista desde scripts de test cuando sea posible; el proyecto `The Fan-tasy Tileset` se lee de su ruta local y no se copia al repo sin revisar la licencia y añadir la trazabilidad en `assets/README.md`.

### 10.2 Gates

- **Host**: IR contra fixtures; round-trip de GID/flips; catálogo `VisualId`; páginas y máscaras; capas `Baked`/`Overlay`/`Occluder`; reloj de animación; colisión; proyección/picking iso; presupuesto de bytes.
- **Referencia visual**: `tmxrasterizer` por capa y mapa completo; comparación 100 % con el preview del cooker; cualquier diferencia se explica o falla.
- **On-target**: demos de §10.3 con `measure-fps`, perfil, flicker y secuencia de frames.
- **Visión**: modelo local sobre secuencias (no una captura suelta) para scroll, agua/flores animadas, personaje oculto por puente/edificio y orden iso. Una demo no se declara VERIFICADA sin `VALIDATION.md` con prompts, respuestas y medidas.

### 10.3 Demos previstas

| Demo | Nivel | Qué valida |
|---|---|---|
| `206_tiled_beginning_fields_baked` | N1 | Cooker, paleta, base cocida, cámara X/Y, 50 fps |
| `207_tiled_live_layers_animation` | N2 | `Overlay` de agua/flores, pase de animación, presupuesto de celdas |
| `208_tiled_occlusion_objects` | N2 | `Occluder` tras actores, objetos tipados, colisión, profundidad |
| `209_tiled_iso_room` | N3a | Proyección/picking/orden iso, sala cocida, actores |
| `210_tiled_stream_world` | N4 | Chunks/páginas con prefetch, pin y eviction, mapa grande |

Los números son una reserva de trabajo; se confirman con `node tools/check/next-number.mjs demos/techniques/amiga/playfield` al crear cada demo.

## 11. Límites y rechazos explícitos

- Sin parser XML/TSX/PNG ni cuantización en el Amiga; sin dependencia de Qt/Tiled en runtime.
- Los proyectos `.world` de Tiled (conjunto de mapas) no forman parte de N0–N4: cada `.tmx` se cuece como un nivel/bundle independiente.
- Sin mapas hexagonales, *staggered*, hex side length ni rotaciones hexagonales.
- Sin mezcla alfa general, *tint* ni opacidad de capa/grupo en runtime; se hornea o se rechaza.
- Sin `zstd` hasta que el host lo soporte; el resto de compresiones de Tiled se validan.
- Sin cambios `Baked`↔`Base`/`Overlay` en runtime si el bundle no contiene las formas necesarias; el modo se decide al cocinar.
- Sin más de dos campos de scroll independientes en OCS; el resto es composición por Blitter o se rechaza.
- Sin scroll isométrico continuo en la primera entrega; sala cocida y presupuesto medido antes.
- Sin páginas de tiles en RAM no-Chip para DMA; el staging CPU se copia a Chip antes de publicar.
- Las capas de imagen se cuecen como bitmap estático; una animación de imagen personalizada se convierte a frames/BOBs solo si el cooker la reconoce.
- Sin confundir el PNG de preview del mapa con un atlas de tiles; el cooker usa los TSX/imágenes fuente.

## 12. Compatibilidad y migración

- `WorldMap` v1, `WorldView`, `WorldLayerSource`, `WorldMapChunkLoader`, `StreamingWorldMap` y `TileMapView` se conservan; un `MapBundle` usa `WorldMap` v1 para capas de banco único y `MapLayerCells`/`WorldMap` v2 para capas con catálogo/páginas, no un formato monolítico nuevo.
- `XlimitedScene`/`XlimitedScrollLayer` mantienen sus firmas y defaults; las extensiones de pila/máscara/animación/oclusión son aditivas y se seleccionan por configuración o tipo de presenter.
- `TileScroll`/`StripScrollLayer` siguen consumiendo `TilemapView`; un `MapLayerView` adapta una capa del bundle al mismo contrato para reutilizar el camino de tiras.
- `scene::World` sigue siendo el contenedor de actores/capas retenidas; `Level` puede emitir objetos a un `ActorStore`/`World` y el juego decide qué actores crea. El mapa no posee el mundo.
- El parser regex actual se mantiene como compatibilidad histórica hasta que T1 cierre el parser estructural; el nuevo cooker no depende de él. La retirada se decide al final de T3, no antes.

## 13. Referencias

- [`ROADMAP_TILED_WORLD_SUPPORT.md`](../../guides/roadmap/ROADMAP_TILED_WORLD_SUPPORT.md) — fases, gates y estado del plan.
- [`TILED.md`](../../guides/roadmap/TILED.md) — subconjunto actual del importador y límites conocidos.
- [`WORLD_FORMAT.md`](WORLD_FORMAT.md) — formato `WorldMap` v1 y su mapeo al runtime.
- [`CONTENT_AND_TILEMAP.md`](CONTENT_AND_TILEMAP.md) — modelo de contenido, tiles y mundo disperso.
- [`PLAYFIELD_SCROLL_ARCHITECTURE.md`](PLAYFIELD_SCROLL_ARCHITECTURE.md) — separación algoritmo/superficie/composición/máquina.
- [`SCROLL_VARIANTS.md`](SCROLL_VARIANTS.md) — variantes de scroll y huecos de `SubTileFill`/Y/XY.
- [`XYLIMITED_ALGORITMO_GENERICO.md`](XYLIMITED_ALGORITMO_GENERICO.md) — invariantes del corkscrew.
- [`PIXEL_ART_2D_ISOMETRIC.md`](PIXEL_ART_2D_ISOMETRIC.md) — proyección, picking, profundidad y colisión iso.
- [`PUBLIC_GAME_API.md`](PUBLIC_GAME_API.md) y [`ROADMAP_GAME_API.md`](../../guides/roadmap/ROADMAP_GAME_API.md) — fachada por intenciones y planner de cámara/tilemap.
- [`TILED_MAP_RUNTIME.md`](TILED_MAP_RUNTIME.md) — este documento.
- Proyecto Tiled local: `C:\Users\dvdjg\Documents\programa\tiled`; especificación `docs/reference/tmx-map-format.rst`, `docs/reference/tmx-changelog.rst`, `docs/reference/support-for-tmx-maps.rst`; render de referencia `src/tmxrasterizer`.
- Fixture local: `C:\Users\dvdjg\Documents\programa\Assets\2D\The Fan-tasy Tileset (Free) 1.5.9\Tiled\Tilemaps\Beginning Fields.tmx` y su preview `Beginning Fields.png`; revisar licencia antes de versionar arte.
