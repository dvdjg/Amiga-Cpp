# Roadmap: soporte de mundos Tiled en el engine

Plan de adopción para convertir proyectos de Tiled (`.tmx`/`.tsx` e imágenes) en mapas cocinados que el engine monta y renderiza en Amiga sin parsear XML en runtime. El **modelo runtime, los contratos internos y la API de intenciones** están en [`TILED_MAP_RUNTIME.md`](../../engine/architecture/TILED_MAP_RUNTIME.md); este documento fija el alcance, el orden de trabajo, los gates y las demos. El subconjunto que entiende el importador actual está en [`TILED.md`](TILED.md).

## 1. Objetivo y límites

- Admitir mapas **ortogonales** finitos e infinitos y, en una fase posterior, **isométricos** de sala y de scroll.
- Soportar plataformas, *shooters* cenitales, CRPG top-down y CRPG/acción isométrica con un modelo de capas que no destruya el presupuesto del A500.
- Cocinar tilesets, imágenes, paletas, animaciones, objetos y colisión en host; el runtime consume un bundle binario versionado y validado.
- Mantener **IDs estables** de tile/capa/objeto: la residencia en Chip (páginas, chunks) no cambia el significado del mapa.
- Ofrecer una **capa de intenciones de alto nivel** (`eng::Level`) como puerta principal para cargar, consultar y gestionar el mapa; los motores concretos quedan detrás.
- Dejar abierta la composición de capas «tal cual» para prototipos y equipos que la pidan, pero el **perfil A500 por defecto** cuece las capas estáticas y solo mantiene vivas las necesarias.
- No implementar Tiled en el Amiga: XML, TSX, PNG, cuantización y compresión de intercambio se resuelven en host.

## 2. Estado de partida

- `tools/ehb/parse-tmx.mjs` lee un subconjunto de TMX con expresiones regulares: mapas finitos/infinitos, CSV/XML/base64 gzip/zlib, resolución básica de TSX y limpieza de flips. No es un parser estructural y no lee grupos, propiedades, animaciones, objetos ni colecciones de imágenes.
- `tools/ehb/gid-to-bank.mjs` y `tools/ehb/pack-world.mjs` convierten **una capa** y **un tileset/banco** a `WorldMap`; hay round-trip host. El origen lógico se normaliza y no se conserva para objetos/cámara.
- `engine/include/eng/assets/uaf.hpp` (`WorldView`) y `eng/field/world_layer.hpp` (`WorldLayerSource`, `WorldMapChunkLoader`) montan capas `WorldMap` sobre `SparseTileMap`/`StreamingWorldMap`/`TileMapView`.
- `tools/amiga-tiles/amiga-tiles.mjs` cuantiza una imagen (2..255 colores, EHB, dither, dedupe) y genera tilebanks indexados/interleaved. No conoce Tiled ni cocina por capa/tileset.
- `out/assets/beginning-fields/8c` contiene un banco de 8 colores extraído del **PNG de preview** del mapa (1180 tiles únicos), no de los TSX del proyecto. Es una referencia de calidad, no la salida objetivo del pipeline Tiled.
- `XlimitedScene`/`XlimitedPlayfield`/`XlimitedScrollLayer` cubren un mapa por campo, single/DPF, lienzo FG y HUD; no tienen pila de capas de tiles, máscaras de overlay, parcheo de animación ni pase de oclusión tras actores.
- `TileScroll`/`StripScrollLayer` cubren scroll de tiras con un `TilemapView`; `WorldTileMapView` adapta una capa de `scene::World` al contrato `TileMap`.
- No existe todavía `eng::Level` ni un catálogo runtime `VisualId` → página/tile con caché de páginas separada del `ChunkCache` lógico.

## 3. Decisiones de arquitectura

Las decisiones canónicas y su justificación están en [`TILED_MAP_RUNTIME.md`](../../engine/architecture/TILED_MAP_RUNTIME.md) §2. Resumen operativo para este roadmap:

- **D1**: Tiled es autoría; el runtime no parsea TMX/TSX/PNG. Un cooker host produce UAF-R + manifiesto + informe.
- **D2**: una sola tubería y una sola fuente de verdad; `png2amiga` solo como referencia de algoritmos host, no como segunda tubería.
- **D3**: IDs estables (`VisualId`/`LayerId`/`ObjectId`); nunca slots de caché en las celdas.
- **D4**: política de capas explícita (`Baked`, `Base`, `Overlay`, `Occluder`, `Parallax`, `CollisionOnly`); estáticas cocidas por defecto.
- **D5**: XYLimited es el presenter ortogonal; el isométrico es un presenter aparte (sala cocida primero).
- **D6**: base opaca + overlays enmascarados + pase de oclusión tras actores; DPF solo para un campo de paralaje cuando encaje.
- **D7**: DMA tipado por banco (`Chip`); sin `Span` agnóstico en la frontera de render.
- **D8**: perfiles de compilación para eliminar cachés/streaming cuando el mapa cabe residente.

## 4. Matriz de soporte (resumen)

La matriz detallada, con semántica y límites, está en [`TILED_MAP_RUNTIME.md`](../../engine/architecture/TILED_MAP_RUNTIME.md) §1 y §11. Resumen:

| Función de Tiled | N0 | N1 | N2 | N3 | N4 | Notas |
|---|---|---|---|---|---|---|
| Ortogonal finito/infinito | Sí | Sí | Sí | — | Sí | Chunks negativos con origen lógico |
| Isométrico de sala | Sí | — | — | Sí | — | N3a; scroll iso en N3b |
| Capas `tile` | Sí | Sí | Sí | Sí | Sí | `Baked`/`Base`/`Overlay`/`Occluder` |
| Capas `group`/`image` | Sí | Sí | Sí | Sí | Sí | Opacidad/tint se hornean o se rechazan |
| Tilesets atlas/colección | Sí | Sí | Sí | Sí | Sí | Imágenes de colección como sprites |
| Animación de tile | Sí | — | Sí | Sí | Sí | Parche por Blitter; frames agrupados |
| Objectgroup / tile objects | Sí | Sí | Sí | Sí | Sí | Tipos + propiedades + colisión |
| Colisión por tile/objeto | Sí | Sí | Sí | Sí | Sí | Bitset/materials + shapes |
| Flips H/V/D | Sí | Sí | Sí | Sí | Sí | Variantes precocinadas; runtime como escape |
| Hex/staggered/zstd | — | — | — | — | — | Rechazo explícito |

## 5. Fases

Cada fase termina con un gate reproducible. Las fases no se solapan en implementación: un cambio de formato o de contrato cierra antes de que la demo dependa de él.

### T0 — Fixtures, IR y contrato de soporte

- Fijar la versión de Tiled objetivo (1.10/1.11), la lista de funciones soportadas y la lista de rechazos con diagnóstico.
- Definir el esquema `eng.tiled-ir` v1, el `cook-plan` y el manifiesto del bundle.
- Crear fixtures deterministas: ortogonal finito, infinito con chunks negativos, isométrico de sala, grupo con opacidad, tile animado, colección de imágenes, flips H/V/D, objectgroup con polígonos, zstd (rechazo).
- Inventariar `Beginning Fields` (capas, tilesets, animaciones, objetos, colisión) y escribir su `cook-plan`.
- Usar `tmxrasterizer` del proyecto Tiled para golden renders completos y por capa.
- **Gate**: los fixtures existen y están catalogados; el esquema IR/cook-plan está fijado; el golden render es reproducible; cada rechazo previsto tiene diagnóstico. La validación de parseo se cierra en T1.

### T1 — Parser estructural e IR

- Sustituir la extracción por regex por un parser XML estructural (host-only), con entidades, anidamiento y rutas relativas.
- Resolver TSX externos e inline, atlas y colecciones, `firstgid`/`tilecount`/`columns`/`margin`/`spacing`/`tileoffset`/`objectalignment`.
- Leer `layer`, `objectgroup`, `imagelayer` y `group` con visibilidad, opacidad, offsets, paralaje, propiedades y clases; preservar propiedades opacas. Resolver plantillas de objeto (`template`) en host o rechazarlas explícitamente.
- Interpretar GID completo: `id = gid & 0x1FFFFFFF`, flags H/V/D, rechazo de rotaciones hexagonales; conservar `origin` de mapas infinitos.
- Validar conteos, longitudes, índices y rutas; emitir `diagnostics` con severidad y localización.
- **Gate**: IR equivalente a Tiled en los fixtures; ningún atributo soportado se pierde en silencio; los rechazos son accionables.

### T2 — Cooker de tilesets, paleta y páginas

- Recolectar solo los tiles referenciados (capas + objetos + frames + flips usados).
- Cuantizar contra una paleta global por campo (16/32/EHB/DPF) y medir la calidad con `compare-palettes.mjs` + visión.
- Generar páginas de tiles con layout DMA, dedupe, máscara 1bpp para overlays y alineación/guarda; agrupar frames de animación.
- Cocinar objetos de colección como sprites/BOBs con tamaño variable, anclaje y máscara.
- Producir el informe de bytes Chip/Fast y el preview de reconstrucción.
- **Gate**: reconstrucción índice a índice al 100 %; preview de contenido equivalente a Tiled con las diferencias de paleta documentadas; informe de página/paleta; sin tiles huérfanos ni duplicados.

### T3 — Cooker de mapa y `MapBundle`

- Clasificar capas (`Baked`/`Base`/`Overlay`/`Occluder`/`Parallax`/`CollisionOnly`) desde propiedades + `cook-plan`.
- Fundir las capas estáticas en el fondo base respetando orden, offsets, opacidad horneada y paleta.
- Emitir `WorldMap` v1 para capas de banco único y `MapLayerCells` (o `WorldMap` v2) para capas con catálogo/páginas, más los chunks nuevos: `MapManifest`, `MapLayers`, `TileCatalog`, `TilePages`, `TileAnimations`, `MapObjects`, `Collision` (nombres; tipos/números se fijan al implementar).
- Asignar `VisualId`, `LayerId`, `ObjectId`, `PageId` estables; conservar origen lógico y wrap; resolver coordenadas negativas con floor division.
- Validar el bundle y el presupuesto; fallar si una capa `Occluder` se intenta hornear o si la paleta no encaja.
- **Gate**: bundle determinista (hash), round-trip de mapa/capas/objetos, presupuesto declarado, preview por capa.

### T4 — Runtime de contenido y catálogo

- Añadir `eng::map::AssetView` (validación del bundle), `TileCatalog`/`TileResolver`, `LayerCellSource` residente y `MapRuntime`.
- Adaptar `WorldLayerSource`/`WorldMapChunkLoader` para capas del bundle sin romper `WorldView`.
- Añadir `TilePageCache` residente (sin LRU todavía) con pin por frame y vistas Chip tipadas.
- Exponer consultas de celda/colisión y un `LevelStats` mínimo.
- **Gate**: host tests de validación, catálogo, resolución de página, capas y pin; un mapa residente se monta sin heap ni XML.

### T5 — Presenter XYLimited extendido

- `TileLayerStack`: base + overlays por banda entrante; cada overlay con su máscara y orden `z`.
- Pase de animación `TileAnimator` con presupuesto de celdas visibles y parche enmascarado.
- Pase `after_actors` para `Occluder`, con dirty rects de actores/cámara/animación.
- Integración con `ScrollLayer`/`App`: un nivel se registra y el `App` lo conduce; visibilidad/modo validados contra el bundle.
- DPF/paralaje: un segundo campo cuando la paleta y la prioridad lo permitan; sin prometer N campos.
- **Gate**: demos 206/207/208; 50 fps sin tirones en el alcance medido; flicker; visión de secuencia; no regresión de 201–205.

### T6 — Objetos, colisión y gameplay

- Emitir `MapObjects` tipados: spawn, trigger, collider, path, camera, door, mechanism, decor; propiedades y clases.
- Resolver *tile objects* con `objectalignment`, `tileoffset`, flips y tamaño variable; convertirlos a `ActorDesc`/BOB o a decor cocido.
- Extraer colisión de tiles y de objetos (polígonos de TSX) a bitsets/materials/shapes; consultas O(1).
- Integrar con `scene::World`/`ActorStore` y con el planner de actores; el mapa no posee los actores.
- **Gate**: host tests de anclaje/colisión; demo 208 con personaje que pasa por debajo de un occluder y colisiona con el escenario.

### T7 — Isométrico

- Implementar `IsoProjection` (forward/inverse), picking de rombo y `DepthKey` (`u+v`, elevación, sublayer, tie-break).
- Cocer salas iso estáticas a bitmaps/bandas con la paleta elegida; actores ordenados por foot point; occluders como BOBs de primer plano.
- Soportar `tileoffset`, `objectalignment` y flips/rotaciones precocinadas; informar del coste de variantes.
- Dejar N3b (scroll iso continuo) como presenter separado con presupuesto medido; no forzarlo en XYLimited.
- **Gate**: golden tests de proyección/picking/orden; demo 209 con sala, actores y oclusión; visión de secuencia.

### T8 — Streaming y mundos grandes

- `TilePageCache` LRU con slots fijos, request/pending/ready, pin/fence y generación de slot.
- `MapChunkCache`/`StreamingWorldMap` para celdas lógicas; prefetch de la ventana visible + guarda + overhang de objetos/arte.
- Carga asíncrona desde `BackgroundQueue`/VFS; publicar completions por mensaje; nunca bloquear el render.
- Invalidación por tile editado, animación, recarga de tileset y cambio de paleta.
- **Gate**: tests de residencia, pin durante Blit, eviction/reload en otro slot, page miss no bloqueante y recuperación de error; demo 210 con mapa grande.

### T9 — Cierre de API y endurecimiento

- Consolidar `eng::Level` como fachada única: carga, cámara, capas, animación, objetos, colisión, presupuesto y stats.
- Retirar el parser regex o declararlo legacy; actualizar `TILED.md` y los índices.
- Completar `VALIDATION.md` de 206–210 con medidas, flicker y visión; registrar deuda restante.
- **Gate**: un juego de ejemplo carga un mapa Tiled y usa `Level` sin nombrar tipos internos; demos verificadas; documentación enlazada.

## 6. Fixtures y demos

- **Fixture principal**: `Beginning Fields.tmx` (40×40, 16×16, 7 capas de tiles, 2 objectgroups, 18 tilesets, 169 objetos, animaciones de agua/flores/campfire, polígonos de colisión). El PNG `Beginning Fields.png` es el render plano del mapa y se usa como referencia visual; la fuente de tiles son los TSX y sus imágenes.
- **Puente sintético**: capa `Occluder` sobre base + actor; mide el pase `after_actors` y la restauración al salir.
- **Iso de sala**: mapa pequeño con objetos altos y picking; mide orden y oclusión.
- **Infinito/negativo**: chunks con coordenadas negativas; mide origen lógico y floor division.
- **Flips/colecciones**: tiles H/V/D y tileset de colección; mide catálogo y anclajes.
- **Rechazos**: zstd, hex/staggered, opacidad no horneable, paleta incompatible, GID fuera de rango; cada uno con diagnóstico.

Demos previstas (números a confirmar con `tools/check/next-number.mjs`):

| Demo | Nivel | Gate |
|---|---|---|
| `206_tiled_beginning_fields_baked` | N1 | Cooker + base cocida + cámara X/Y + 50 fps + visión |
| `207_tiled_live_layers_animation` | N2 | Overlays + agua/flores animadas + presupuesto de celdas |
| `208_tiled_occlusion_objects` | N2 | Occluder tras actores + objetos + colisión + profundidad |
| `209_tiled_iso_room` | N3a | Proyección/picking/orden iso + sala + actores |
| `210_tiled_stream_world` | N4 | Chunks/páginas + prefetch/pin/eviction + mapa grande |

## 7. Criterios de aceptación global

1. Un proyecto Tiled soportado se convierte en un bundle determinista sin requerir XML/TSX/PNG ni cuantización en el Amiga.
2. El cooker no pierde capas, flips, propiedades soportadas, animaciones, objetos ni coordenadas negativas; lo no soportado falla con diagnóstico.
3. Las capas estáticas se cuecen por defecto; las vivas se limitan a animación/visibilidad/paralaje/oclusión y su coste se mide.
4. Un `Occluder` se dibuja después de los actores y no se hornea; un puente/edificio tapa y destapa al personaje correctamente en secuencia.
5. Los tiles animados se parchean por Blitter con presupuesto; no se recompone la capa completa cada frame.
6. XYLimited ortogonal conserva sus invariantes y demos; el isométrico usa un presenter propio y no rompe el contrato de scroll.
7. Un mundo grande no bloquea el render por un page miss; IDs estables y páginas pinneadas durante DMA.
8. `eng::Level` permite a un juego cargar y gestionar el mapa sin nombrar `WorldView`, `TileCatalog`, `TilePageCache` ni registros.
9. Las demos 206–210 quedan VERIFICADAS con fps/cadencia/flicker y visión de secuencia, o documentan su deuda y alcance no verificado.
10. Los consumidores actuales (`WorldMap` v1, `XlimitedScene`, `TileScroll`, demos 201–205) siguen funcionando sin cambios de contrato.

## 8. Riesgos y decisiones abiertas

- **Calidad de paleta**: 16 colores puede ser insuficiente para los 18 tilesets de `Beginning Fields`; hay que comparar 16/32/EHB/DPF con visión antes de fijar el perfil. La decisión afecta a bytes y a la composición.
- **Opacidad/tint de Tiled**: no hay blending alfa general en el camino caliente; se hornea contra la capa inferior o se rechaza. Falta decidir el comportamiento por defecto para grupos con opacidad.
- **Objetos grandes**: las imágenes de colección (edificios de 175×128) no son tiles; decidir si se hornean, se convierten en BOBs o se comparten como sprites. Afecta a memoria y a profundidad.
- **Máscaras**: la máscara 1bpp de overlay añade memoria y un fetch de Blitter; medir si conviene generarla para todos los tiles o solo para los que se usan en overlays.
- **Oclusión**: el redibujado de occluders tras actores puede ser caro si el actor se mueve mucho; el presupuesto de dirty rects y la política de “solo intersección” deben validarse en la demo 208.
- **Isométrico con scroll**: N3b no está dimensionado; puede requerir un presenter con anillo/proyección y un presupuesto distinto. No prometerlo hasta medirlo.
- **`zstd`**: si un proyecto de Tiled lo usa, hace falta una dependencia host o un rechazo claro; decidir en T0.
- **Licencia del tileset**: `The Fan-tasy Tileset (Free) 1.5.9` se lee de su ruta local; antes de copiar arte al repo hay que revisar la licencia y añadir trazabilidad en `assets/README.md`.
- **Nombre/forma final de `eng::Level`**: la fachada objetivo está definida por intención; los parámetros de plantilla y el alias por defecto se cierran en T9 con un juego real.

## 9. Orden de implementación

T0 fixtures/IR → T1 parser estructural → T2 cooker de tilesets/paleta → T3 cooker de mapa/`MapBundle` → T4 runtime/catálogo → T5 presenter XYLimited extendido (demos 206–208) → T6 objetos/colisión → T7 isométrico (209) → T8 streaming (210) → T9 cierre de API. Cada fase actualiza `TILED_MAP_RUNTIME.md` solo si cambia un contrato; las notas de transición y los descartes van a `docs/debugging/` o a la bitácora, no al documento de referencia.

## 10. Referencias

- [`TILED_MAP_RUNTIME.md`](../../engine/architecture/TILED_MAP_RUNTIME.md) — modelo runtime, formato, contratos y API de intenciones.
- [`TILED.md`](TILED.md) — subconjunto actual del importador y límites.
- [`WORLD_FORMAT.md`](../../engine/architecture/WORLD_FORMAT.md) — `WorldMap` v1 y mapeo al runtime.
- [`CONTENT_AND_TILEMAP.md`](../../engine/architecture/CONTENT_AND_TILEMAP.md) — contenido, tiles y mundo disperso.
- [`PLAYFIELD_SCROLL_ARCHITECTURE.md`](../../engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md) — algoritmo/superficie/composición/máquina.
- [`SCROLL_VARIANTS.md`](../../engine/architecture/SCROLL_VARIANTS.md) — variantes y huecos de scroll.
- [`XYLIMITED_ALGORITMO_GENERICO.md`](../../engine/architecture/XYLIMITED_ALGORITMO_GENERICO.md) — invariantes del corkscrew.
- [`PIXEL_ART_2D_ISOMETRIC.md`](../../engine/architecture/PIXEL_ART_2D_ISOMETRIC.md) — proyección, picking y profundidad iso.
- [`PUBLIC_GAME_API.md`](../../engine/architecture/PUBLIC_GAME_API.md) y [`ROADMAP_GAME_API.md`](ROADMAP_GAME_API.md) — fachada por intenciones.
- [`ROADMAP_UNIFICADO.md`](ROADMAP_UNIFICADO.md) — estado vigente del engine y las demos.
