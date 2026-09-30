# Roadmap: soporte de mundos Tiled en el engine

Plan para convertir proyectos de Tiled (`.tmx`/`.tsx` y assets referenciados) en mundos cocinados que
el engine pueda montar y renderizar en Amiga sin parsear XML durante el juego. Parte del parser y
packer de mundo existentes; no asume que todo el formato Tiled deba entrar en runtime.

## Objetivo y límites

- Admitir mapas ortogonales e isométricos como contenido host-cocinado.
- Usar `WorldMap`/`TileSource` como geometría y referencias lógicas, extendiendo el formato cuando
  haga falta metadata, capas o IDs estables.
- Cocinar tilesets e imágenes a paletas y bitplanes Amiga (OCS/ECS/AGA/EHB) en host.
- Mantener separados el cache de chunks de mapa y el cache de tileset/tiles DMA residentes.
- Seleccionar la estrategia de superficie/scroll adecuada: X-Limited para movimiento horizontal,
  Y-Limited para vertical cuando exista una implementación eficiente, XY-Limited para ambas; un
  juego iso usa proyección/render order propios y puede usar cualquiera de esas superficies según el
  movimiento/cámara.
- Permitir mundos mayores que Chip RAM con prefetch y eviction que no bloqueen el render.
- No implementar Tiled en el Amiga: XML, TSX, imágenes, compresión de intercambio y propiedades se
  resuelven en host; runtime consume chunks binarios versionados.

## Estado de partida

- `tools/ehb/parse-tmx.mjs` lee un subconjunto de TMX con regex: mapas finitos/infinitos,
  CSV/XML/base64 gzip/zlib y resolución básica de TSX.
- `gid-to-bank.mjs` y `pack-world.mjs` convierten una capa a índices de banco y `WorldMap` disperso;
  hay round-trip host.
- `WORLD_FORMAT.md` y `WorldView` representan capas de tiles en chunks con directorio ordenado.
- `TileSource`, `ChunkCache`, `StreamingWorldMap` y `TileMapView` dan acceso solo-residente y una
  política de chunks de capacidad fija.
- `slice-tiles.mjs`/`amiga-tiles` cuantizan imágenes y generan tilesets indexados; el banco
  X-Limited interleaved es un caso especializado.
- El soporte no equivale todavía a un pipeline Tiled general. `parse-tmx` es extractor de datos, no
  parser XML robusto ni cooker de todos los tipos de capa, objetos y propiedades.

## Modelo de datos objetivo

```text
Tiled source project
  ├── map: orientation, dimensions, layers, chunks, properties, objects
  ├── tileset catalog: firstgid, local tile id, image/collection, per-tile properties/animation
  └── external source assets: images, TSX, object templates
          │ host parser + cooker
          ▼
CookedWorld (UAF-R / chunks)
  ├── WorldMap: sparse cell references + layer metadata
  ├── TilesetCatalog: stable tileset id, tile geometry, palette/material, page table
  ├── TilePages: indexed/palette-quantized assets in Amiga-ready layout
  ├── Collision/Navigation metadata
  ├── Objects/Triggers/Mechanisms
  └── Animation/Properties needed at runtime
          │
          ├── map-chunk cache (CPU-visible logical cells)
          └── tile-page cache (Chip-resident DMA images)
```

Las celdas deben almacenar una referencia estable, por ejemplo `{tileset_id, local_tile_id, flags}`
o un `TileRef` global asignado por el cooker. No se almacena el índice de una ranura temporal del
cache: una eviction/reload no puede cambiar el significado de las celdas. Las operaciones de flip
son flags explícitos y se materializan por variante precocinada, transformación de la copia o
pipeline; no se descartan silenciosamente.

La metadata de mapa debe guardar orientación y convención de coordenadas. Para iso debe distinguir
`tilewidth/tileheight` de tamaño de artwork, `objectalignment`, offsets, origen de chunk, elevation y
orden de dibujo. La proyección isométrica convierte coordenadas lógicas para render; no debe
mezclarse con los índices del mapa ni con el espacio de colisión.

## Pipeline host

### T0 — Inventario y fixtures de conformidad

- Crear fixtures mínimos exportados por versiones soportadas de Tiled: ortogonal finito, ortogonal
  infinito con chunks negativos, iso finito/infinito, varios TSX, tile flip, tile animado,
  objectgroup, properties y tileset de colección de imágenes.
- Fijar versión Tiled, formatos soportados y rechazos explícitos.
- Comparar los GID originales con la interpretación de flags usando una reconstrucción visual
  determinista.
- **Gate**: fixtures host reproducibles; errores de formato con path/capa/chunk identificados.

### T1 — Parser XML y modelo intermedio

- Sustituir extracción por regex por parser XML estructural que respete jerarquía/nesting, entidades,
  rutas relativas y errores de sintaxis.
- Resolver TSX externos relativos al TMX, múltiples tilesets, intervalos `firstgid`, `tilecount`,
  `columns`, imágenes, image collections, offsets y properties.
- Soportar capas tile, object, image y group con visibilidad, opacidad, offset, parallax, tint y
  propiedades; preservar en IR solo atributos seleccionados por el cooker.
- Interpretar completamente los bits de GID: separar ID local de horizontal/vertical/diagonal y
  rotación hexagonal donde aplique. Rechazar combinaciones no soportadas; no enmascarar flags como
  si fueran parte del índice.
- Validar tamaños y conteos decodificados frente a `width*height` o `chunk.width*chunk.height`;
  validar que el base64 comprimido termina en longitudes coherentes.
- **Gate**: IR canónico host más report de warnings/errors y render preview equivalente a Tiled para
  los casos soportados.

### T2 — Cooker de tilesets y activos Amiga

- Decidir paleta por escena/tileset/layer según hardware y límite de playfields; cuantizar todos los
  tiles que comparten una paleta contra la misma tabla.
- Cortar, deduplicar y asignar IDs estables. Generar flips/variantes offline cuando ahorren trabajo
  por frame y el uso de memoria sea aceptable.
- Producir páginas (`TilePage`) con número fijo de tiles y disposición de DMA: planar separado,
  interleaved o el layout especializado elegido por driver. Incluir máscara si hay transparencia,
  padding/guarda requerido por shift y alineación de palabra.
- Empaquetar píxeles indexados solo en el archivo de staging CPU si reduce almacenamiento; los tiles
  publicados al Blitter deben estar expandidos a Chip en formato que el driver consuma directamente.
- Si se usa EHB, exportar índices bases-primero y metadatos de paleta consistentes. No mezclar
  tilesets con paletas incompatibles en un mismo playfield sin una regla de material/cambio Copper.
- Cocinar animaciones de tile como secuencia de `TileRef` y duración fija, no como XML.
- **Gate**: reconstrucción color-index exacta; estimación de bytes Chip por página/buffer/paleta;
  comprobación de transparencias, shifts y alineación.

### T3 — WorldMap multicapa y sparse

- Generalizar `WorldMap` para descriptores de capas con `TileRef`, orientación, flags, bounds y
  referencia a tileset catalog; conservar modo compatible con mapas existentes de un único banco.
- Mantener directorio sparse ordenado por `(cy,cx)`; chunks ausentes representan el valor vacío, no
  memoria implícitamente reservada.
- Admitir coordenadas negativas de Tiled mediante origen lógico explícito y conversión estable a
  coordenadas de chunk con floor division, no truncamiento hacia cero.
- Rechazar o segmentar spans extremos que hagan el bounding rectangle imposible de representar,
  aunque haya pocos chunks poblados.
- Separar datos por semántica: visual cells, collision/navigation flags y objetos/triggers pueden
  estar en chunks paralelos y cargarse con distinta prioridad.
- **Gate**: equivalencia de mapas finitos/infinitos, coordenadas negativas, chunk ausente, capas y
  IDs de tileset estables en reordenamiento del catálogo.

### T4 — Capas, orientación y scroll

- **X-Limited**: mundo mayormente horizontal, cámara desplaza X, bandas/columnas entrantes; usar el
  driver actual cuando sus límites/layout coincidan.
- **Y-Limited**: mundo principalmente vertical (plataformas verticales, ascensores/torres). Diseñar
  y medir filas entrantes, guarda vertical, coarse/fine y doble buffer; no asumir que activar un
  booleano Y en XY-Limited produce el coste óptimo.
- **XY-Limited**: movimiento en ambos ejes con anillo/corkscrew, staging y split de Copper; caso más
  general y con presupuesto mayor.
- **Isométrico**: el mapa lógico sigue siendo grid/chunks; el painter transforma y ordena por
  profundidad. Definir vista rectangular/romboidal de celdas visibles y prefetch que incluya tiles
  ocultos por el footprint de sprites/overhang de artwork.
- Determinar por setup/plantilla el scroll mode y eliminar ramas/cálculos de estrategias no usadas.
- **Gate**: demos comparativas X-only, Y-only, XY y iso; verificar costuras, paleta, regiones y
  presupuesto Copper/Blitter.

## Tilesets grandes y streaming

Mapa disperso y tileset grande son problemas distintos y requieren caches distintos:

```text
TMX cells → map chunk cache (Fast/Slow o compacta, según lookup)
TileRef   → catalog permanente pequeño (id → page/local id)
visible TileRef → tile page cache en Chip → Blitter/bitplane DMA
```

El `ChunkCache` existente almacena chunks lógicos en un pool aportado por el llamador. No resuelve
por sí mismo la residency del bitmap del tileset en Chip. Para tilesets que no caben completos:

- Mantener catalog, page directory y mapeo `TileRef→page` residentes en Fast si existe; si no, usar
  Slow/Chip según el coste y capacidad.
- Mantener un cache LRU de páginas gráficas **Chip** con slots fijos y generaciones. Solo las
  páginas residentes pueden ser origen DMA de `BlitJob`.
- Precargar la unión de páginas necesarias por la ventana visible, guarda del scroll, capas visibles,
  animaciones activas y overhang de artwork, más margen de varios frames.
- Leer/descomprimir a staging CPU si el backend lo permite; copiar/cocinar a layout DMA en un buffer
  Chip antes de marcar la página residente. Para trackloader tras takeover, el buffer de disco sigue
  necesitando Chip y el decode debe coordinarse con el doble buffer de pista.
- Pinnear páginas durante el frame/plan DMA en curso. No evictar una página referenciada por
  `FramePlan`, Blitter activo o pantalla frontal; liberarla tras fence/fin de blit y salida de la
  ventana de prefetch.
- Mantener fallback si una página no está lista: placeholder, tile de error o retrasar la cámara/
  animación según política. `tile_at`/render no debe bloquear esperando I/O.
- Priorizar por distancia a la cámara, tiempo hasta visibilidad, capa, animación y dirección de
  movimiento; agrupar lecturas contiguas por página/sector para reducir seeks.
- Evitar tileset pages diminutas: la granularidad debe equilibrar fragmentación Chip, lectura
  secuencial y el conjunto visible de la cámara.

El mapa persistido debe referenciar IDs de tiles estables, nunca slots LRU. Una página expulsada
puede volver a otra ranura sin reescribir el mundo; el resolver actualiza la residencia en el
catálogo/cache y el render solo emite jobs para refs ya pinneadas.

## T5 — Runtime, resolvers y caches

- Añadir `TilesetCatalog`/`TileRef` que resuelva IDs globales estables a tileset, página, tile local,
  material y flags de flip.
- Añadir `TilePageCache` Chip independiente del `ChunkCache` lógico, con request/pending/ready,
  ref/pin por frame, LRU y generación de slot.
- Añadir `TileRequestSet` por región/capa, compactable en páginas contiguas y reusable entre frames.
- Integrar con `BackgroundQueue`/VFS/resource pipeline; prefetch fuera del render y publicar
  completions por mensaje.
- Añadir dirty/cache invalidation para tile editado, tile animado, tileset reload y palette change.
- **Gate**: tests de residency, pin durante blit, eviction/reload en otro slot, page miss no bloqueante
  y recuperación tras error.

## T6 — Assets y object layers

- Cocinar object layers como spawns, triggers, rutas, puertas, puntos de cámara y colisionadores.
- Convertir properties a schema tipado por juego, preservando extensiones opacas permitidas.
- Resolver tile objects y su origen/alineación antes de convertirlos a `ActorDesc`.
- Traducir shapes de Tiled (rectángulo, polígono, polilínea, ellipse) a colliders/proxies del
  subsistema gameplay, manteniendo coordenadas de mundo y offsets.
- **Gate**: round-trip de metadata seleccionada y fixtures host para objetos/isométricos.

## T7 — Isométrico y transformaciones Tiled

- Interpretar `orientation`, `renderorder`, dimensiones de grid, `staggeraxis`, `staggerindex`,
  `hexsidelength` y `objectalignment` solo en las orientaciones declaradas soportadas.
- Convertir Tiled GID flips a flags de transformación. Para flips/rotaciones que el Blitter no hace
  directamente, escoger variante precocinada o transformación en host, no una conversión por píxel
  en el hot path.
- Implementar `IsoProjection` para ortogonal isométrico y picking inverso, con enteros/fixed y
  redondeo probado para coordenadas negativas.
- Ordenar por foot-point/profundidad y sublayer; soportar artwork con overhang sin confundir tamaño
  visual con tamaño lógico de tile.
- **Gate**: golden images host para picking, orden visual, scroll diagonal y flips, además de demo
  con capa iso sobre el renderer planar.

## T8 — Coste, presupuestos y robustez

- Presupuesto separado: WorldMap/metadata, tileset catalog, páginas Chip residentes, staging,
  copperlists, bitplanes y scratch.
- Reportar page misses, prefetched pages, evictions, bytes pinned, hits de tile/chunk, seeks y
  chunks/pages pendientes.
- Limitar tamaños, conteos, offsets, inflación máxima y recursos por mapa al parsear/cocinar; el
  host falla con diagnóstico preciso y runtime no construye vistas sobre datos truncados.
- Tener modos compile-time: mapa residente sin VFS/cache; chunk streaming sin page streaming; ambos;
  mapas vertical/horizontal/XY/iso. El perfil elimina sistemas no utilizados del binario.
- Verificar ejecución DMA real y no inferir capacidad por tests host únicamente.

## Criterios de aceptación global

1. Un proyecto Tiled soportado se convierte a assets cookeados deterministas sin requerir XML/parser
   en Amiga.
2. El cooker no pierde flags GID, capas, metadatos ni coordenadas negativas soportadas.
3. Un tileset multibanco conserva la paleta/layout apropiados y reporta el presupuesto Chip.
4. Un mundo sparse no reserva su bounding rectangle completo en runtime.
5. Tile pages que faltan se precargan asíncronamente y el render nunca usa memoria no residente.
6. La eviction no invalida IDs de tiles y respeta pages pinneadas por DMA/display.
7. X/Y/XY/iso usan algoritmos declarados y cada perfil supera sus pruebas de equivalencia.
8. Mapas estáticos que caben pueden eliminar completamente streaming/cache en compilación.

## Orden de implementación

T0 fixtures y especificación de soporte → T1 parser/IR → T2 tileset cooker → T3 WorldMap multicapa
sparse → T4 drivers/orientaciones → T5 tile page cache y prefetch → T6 object layers y gameplay
metadata → T7 iso/picking/transform flags → T8 budgets, stress y demos. El roadmap puede dividirse
en hitos por backend de almacenamiento si trackloader o disco imponen restricciones adicionales.

## Referencias

- [`TILED.md`](TILED.md)
- [`WORLD_FORMAT.md`](../../engine/architecture/WORLD_FORMAT.md)
- [`CONTENT_AND_TILEMAP.md`](../../engine/architecture/CONTENT_AND_TILEMAP.md)
- [`PLAYFIELD_SCROLL_ARCHITECTURE.md`](../../engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md)
- [`XYLIMITED_ALGORITMO_GENERICO.md`](../../engine/architecture/XYLIMITED_ALGORITMO_GENERICO.md)
- [`STREAMING_LOADER.md`](../../engine/architecture/STREAMING_LOADER.md)
- [`FILE_SYSTEM_AND_DYNAMIC_LIBRARIES.md`](../../engine/architecture/FILE_SYSTEM_AND_DYNAMIC_LIBRARIES.md)
- [`PIXEL_ART_2D_ISOMETRIC.md`](../../engine/architecture/PIXEL_ART_2D_ISOMETRIC.md)
