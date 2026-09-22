# Batería de tests HOST del engine (algoritmos y APIs puras)

Estos tests compilan **con g++ del host** (el compilador GCC del entorno de
desarrollo del toolchain Amiga, no MSVC) contra `engine/include` y se ejecutan
como un binario nativo de la máquina de desarrollo. Sirven para validar
**algoritmos y APIs puras** (matemáticas, ordenación, tablas, etc.) que no
dependen de hardware y no necesitan WinUAE.

> **Motivación.** El engine se compila para un cruce `m68k-*` (Bartman/VSCode o
> el toolchain del proyecto), pero las cabeceras de `eng/core` son freestanding:
> no usan STL ni dependen del backend. Probarlas con g++ del host es lo más
> rápido y determinista para detectar regresiones en algoritmos puros.

## Dónde está y cómo corre

- Los tests viven en `tests/host/NNN_<nombre>/` con `src/main.cpp`, `README.md`.
- Se compilan con `tools/run-host-tests.sh` (usa `g++` del PATH o la variable
  `CXX`).
- No necesitan `g_eng_run_status` ni canal lateral: terminan con código de
  salida 0 (OK) o distinto de 0 (fallo) y escriben un informe por stdout.

## Convenciones

- El código de los algoritmos que se validan vive en `engine/include/eng/core/`
  y es freestanding (sin STL). Solo el `main.cpp` del test puede usar `printf`
  del host para informar.
- Cada test declara al principio las funciones/buenas prácticas que ejercita.
- Falla con mensaje claro si una aserción no se cumple; el script de regresión
  puede invocar estos binarios y considerar fallo `exit != 0`.
- **Numeración única y no reutilizable.** El prefijo `NNN` de `tests/host/NNN_<nombre>/` es único: un test nuevo toma el **siguiente número libre** (máximo + 1). Si se descubre una colisión, se renumera el test **más nuevo** (el que aún no estaba en el catálogo ni referenciado) al siguiente libre; nunca se cambia el número de un test ya catalogado. Tras renumerar hay que actualizar el título y las rutas dentro del test, sus referencias (p. ej. en `engine/`) y este catálogo. Lo valida `tools/check/test-numbering.mjs` (corre en `tools/run-host-tests.sh`): falla si hay prefijos duplicados o si el catálogo no cuadra 1:1 con los directorios.

## Cómo añadir un test

1. Elige el **siguiente número libre** en `tests/host/NNN_<nombre>/` (máximo + 1).
2. Escribe `src/main.cpp` que `#include <eng/core/...>` y aserta los valores.
3. Añade el `README.md`.
4. Regístralo en el `README.md` de esta carpeta (catálogo) y, si la API que
   cubre sube a `engine/`, enlázalo también desde el doc-map del sistema.

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-000 | [eng_core_math](000_eng_core_math/README.md) | `eng::core::isqrt`, `eng::core::quick_sort`, `eng::core::sort_items`, `Span` (port de `libmisc` de `demoscene-repo-orig`). |
| HOST-001 | [graphics_driver_contract](001_graphics_driver_contract/README.md) | Conceptos `eng::DisplayDriver` y `eng::GraphicsDriver` del ciclo de instalación del display (takeover + install): validación compile-time con `static_assert` sobre un mock, `TileScrollScene`, `XlimitedScene` y compositores DPF. |
| HOST-002 | [raster_intent](002_raster_intent/README.md) | Vocabulario portable de intenciones de display: `Visual`, `CopperIntent`, `SpriteIntent` y concept `Effect` (base de la Oleada 1 de demoscene). |
| HOST-003 | [sprite_allocator](003_sprite_allocator/README.md) | `eng::graphics::SpriteAllocator`: reparto de `SpriteIntent` entre 8 canales con multiplexado vertical y decisión overflow → BOB (paso 4 de `ENGINE_DESIGN.md` §5). |
| HOST-004 | [input](004_input/README.md) | `eng::input::InputAggregator`: estado portable de entrada (pad CD32, ratón, teclado) — paso 6 de `ENGINE_DESIGN.md` §5. |
| HOST-005 | [audio](005_audio/README.md) | `eng::audio::AudioMixer`: asignación de canales de Paula (SampleEvent → AudioPlan) — paso 7 de `ENGINE_DESIGN.md` §5. |
| HOST-006 | [input_decode](006_input_decode/README.md) | `eng::amiga::decode_joystick`: decodificación de los bits de `JOYxDAT` en direcciones (AHRM cap. 8) — backend de entrada. |
| HOST-007 | [input_cd32](007_input_cd32/README.md) | `eng::amiga::decode_cd32_buttons`: decodificación del flujo serie CD32 (9 bits → botones) — backend de entrada. |
| HOST-008 | [game_audio](008_game_audio/README.md) | `eng::audio::SampleBank` + `allow_trigger`: banco de muestras y política de voces (cooldown + límite de instancias) — capa de audio de juego. |
| HOST-009 | [wave_tables](009_wave_tables/README.md) | `eng::audio::sine_byte`/`triangle_byte`/`square_byte`: tablas de forma de onda 8-bit (enteras, sin float). |
| HOST-010 | [lib2d](010_lib2d/README.md) | `eng::retro`: primitivas 2D de lib2d sobre los tipos genéricos (Vec2/Mat2x2/Rect + recorte). |
| HOST-011 | [math3d](011_math3d/README.md) | `eng::math3d`: primitivas 3D (port de `lib3d`) — matrices, rotaciones, proyección. |
| HOST-012 | [assets_uaf](012_assets_uaf/README.md) | `eng/assets/uaf.hpp`: contenedor UAF-R (`Blob`/`Reader`/vistas) — bind/find/data + errores. |
| HOST-013 | [math3d_mesh](013_math3d_mesh/README.md) | `eng::math3d` mesh: `mesh_transform`, `mesh_painter_order` (culling + painter). |
| HOST-014 | [object3d](014_object3d/README.md) | `eng::object3d`: port 1:1 de `lib3d` (`Object3D`, transform, aristas/caras). |
| HOST-015 | [fire_sim](015_fire_sim/README.md) | Fuego de `fire-rgb`: simulación (abajo caliente) + `dualtab` C++23 `constexpr` verificado contra el original. |
| HOST-016 | [ham_scene](016_ham_scene/README.md) | `eng/graphics/composition/compose.hpp`: display planar con repetición de filas (cuadruplicado) + huella estática `row_repeat_words` verificada contra la emisión real — geometría de la copperlist y parametricidad. |
| HOST-017 | [background_task](017_background_task/README.md) | `eng::task::BackgroundQueue`: tareas de fondo cooperativas (progreso/rendimiento, adaptación por `vpos`, prioridad al bucle principal). |
| HOST-018 | [rtc](018_rtc/README.md) | `eng::time::from_tod`: reloj de tiempo real desde el contador TOD de la CIA-A (50/60 Hz, wrap 24 h). |
| HOST-019 | [copper_ext](019_copper_ext/README.md) | Extensiones de Copper para *copper chunky*: `SKIP` (máscara `0xffff`), `MOVE32` (orden `reg+2`/`reg`) y parcheo de instrucciones (`move_at`/`patch_data`). |
| HOST-020 | [sinetable](020_sinetable/README.md) | Tabla de seno 4.12 **exacta** del original (`libmisc/sintab.c`) materializada en `eng/core/sinetable.hpp`; invariantes, simetría, checksum y uso por `eng/retro::sin_q12`. |
| HOST-021 | [plasma_tables](021_plasma_tables/README.md) | Datos del plasma: tablas `tab1/2/3` (`fx4i`·`SIN/COS` verbatim) y paleta 256×RGB12, con checksums frente al original. |
| HOST-022 | [fast_div](022_fast_div/README.md) | `eng::fast_div`/`is_pow2`/`ilog2`/`asr_floor`: división/módulo por potencia de dos sin el libcall `__udivsi3` del 68000. |
| HOST-023 | [limited_axes](023_limited_axes/README.md) | Ejes del `ScrollEngine`: `Ring`/`Finite`/`Off` y `OneDirection` (shooter vertical). |
| HOST-024 | [parallax_offset](024_parallax_offset/README.md) | Offset de fondo parallax/fijo y compensación del Copper split ("soft DPF"); sin saltos de columna. |
| HOST-025 | [tile_source](025_tile_source/README.md) | Concepto `TileSource`: mapa denso (`TileLayerMap`) y **disperso por chunks** (`SparseTileMap`); skip de `empty_tile`. |
| HOST-026 | [chunk_cache](026_chunk_cache/README.md) | `ChunkCache`: chunks residentes con carga bajo demanda y evicción LRU (mundo disperso sin todo el mapa en RAM). |
| HOST-027 | [animation](027_animation/README.md) | `Animation`/`Frame`: avance determinista por ticks de juego, loop/one-shot, desacoplado de la representación. |
| HOST-028 | [representation](028_representation/README.md) | `choose_representation`/`RepresentationAllocator`: elección de sprite/BOB/CPU/playfield por tamaño, preferencia y presupuesto, con reasignación al agotarse. |
| HOST-029 | [streaming_map](029_streaming_map/README.md) | `StreamingWorldMap`: prefetch de la ventana, lectura de solo-residentes, chunk ausente como `empty_tile` y evicciones LRU. |
| HOST-030 | [tile_map_view](030_tile_map_view/README.md) | `TileMapView`: accesor de scroll con límites/wrap sobre un `TileSource` (streaming/disperso); `wrap_period`. |
| HOST-031 | [world_view](031_world_view/README.md) | `WorldView`: chunk `WorldMap` (cabecera, directorio ordenado, celdas, wrap, chunks ausentes, metadatos y validación de bloques). |
| HOST-032 | [scroll_profile](032_scroll_profile/README.md) | `ScrollProfile`: selección estática del scroll (paso por frame, guarda de lookahead, prefill, invariante) con alias `ScrollProgressive`/`ScrollFastN`. |
| HOST-033 | [scroll_burst](033_scroll_burst/README.md) | `snap_to_tiles` y `y_staging_tiles`: avance por tiles completos (dirección laceda) y staging vertical del corkscrew por perfil. |
| HOST-034 | [scroll_burst_equiv](034_scroll_burst_equiv/README.md) | `burst_right` equivalente bit a bit a los sub-pasos de 1 px (dibujos, `save_word`, estado) en varios escenarios. |
| HOST-035 | [world_layer](035_world_layer/README.md) | `WorldLayerSource` (capa de `WorldView` como `TileMap`) y `WorldMapChunkLoader` (Loader-RAM): WorldMap → `StreamingWorldMap` → `TileMapView`. |
| HOST-036 | [scroll_target_emitter](036_scroll_target_emitter/README.md) | Contrato del scroll dividido: `ScrollTarget` (layout) + `ScrollEmitter` (dibujo/costura) = `ScrollSink`; `XLimitedPlayfield` cumple las tres. |
| HOST-037 | [world_uaf](037_world_uaf/README.md) | Cadena UAF-R → `WorldView`: ensamblar blob con chunk `WorldMap` (`BlobWriter`), validar con `Blob` y leer el mundo. |
| HOST-038 | [plane_view](038_plane_view/README.md) | `PlaneView`: soft DPF (front/back/`flip`) con doble buffer opcional de planos; base del `SoftDpfComposition`. |
| HOST-039 | [soft_dpf](039_soft_dpf/README.md) | `SoftDpfComposition`: composición soft DPF (doble buffer + blit de copia del patrón) extraída del playfield. |
| HOST-040 | [internal_types](040_internal_types/README.md) | `eng/core/typed.hpp`: vistas con tag de dominio (`Bytes`/`Words`), unidades fuertes y bases; dominios no intercambiables (fallo de compilación). |
| HOST-041 | [block](041_block/README.md) | `Block<Tag>`: reserva de arena tipada (`LinearArena::allocate_block`, `MemoryBlock::block`); dominios no intercambiables. |
| HOST-042 | [mode_switch](042_mode_switch/README.md) | `ModeSwitchZone`: conmutación de geometría de vídeo a mitad de frame con el orden canónico MI09 (`BPLCON0`→`DDF`→mods→`BPLxPT`). |
| HOST-043 | [xlimited_tile_bank](043_xlimited_tile_bank/README.md) | `XlimitedTileBank`: banco de bloques propio o aliaseado (vista + `MemoryKind`); `Block<Tag>` propaga el `kind` de la reserva. |
| HOST-044 | [big_buffer_scroll](044_big_buffer_scroll/README.md) | `BigBufferScroll`: estrategia de scroll trivial (offset de cámara acotado / anillo); contraste con `ScrollEngine`. |
| HOST-045 | [surface_polygon](045_surface_polygon/README.md) | `Surface::fill_polygon`: rasterizado CPU de polígono convexo por scanline (interior/exterior, triángulos, clip). |
| HOST-046 | [mesh_renderer](046_mesh_renderer/README.md) | `eng::graphics::mesh_renderer`: malla 3D → `Surface` (proyección, culling, relleno). |
| HOST-047 | [lib3d](047_lib3d/README.md) | `eng::lib3d`: visibilidad de caras/aristas + transform+proyección sobre el modelo empaquetado de `object3d`. |
| HOST-048 | [math_fixed](048_math_fixed/README.md) | Escalar fixed-point genérico `eng::math::Fixed` (exponentes, promoción, no-mezcla, dot fusionado). |
| HOST-049 | [math_linalg](049_math_linalg/README.md) | Álgebra lineal genérica `eng::math::Vec`/`Mat`/`Affine` sobre cualquier escalar. |
| HOST-050 | [math3d_exact](050_math3d_exact/README.md) | Bit-exactitud de `math3d` (4096 ángulos, load_rotate + reverse). |
| HOST-051 | [object3d_affine](051_object3d_affine/README.md) | Bit-exactitud de `update_object_transformation` (barrido de ángulos). |
| HOST-052 | [math_generic_scalar](052_math_generic_scalar/README.md) | La librería es agnóstica del escalar: 4.12, 8.8, `float` y un complejo de usuario. |
| HOST-053 | [lib3d_projection](053_lib3d_projection/README.md) | Tabla dorada de la proyección `lib3d::transform_vertices` a ángulo fijo. |
| HOST-054 | [light_ops](054_light_ops/README.md) | Sombreado por cara `eng::math::light_ops` e `hi16` contra referencia sin signo. |
| HOST-055 | [inverse_rigid](055_inverse_rigid/README.md) | `math3d::inverse_rigid`: inversa de una transformación rígida (`mT`, `-mT·t`) contra la identidad. |
| HOST-056 | [minifloat16](056_minifloat16/README.md) | `eng::math::MiniFloat16` (`eng/core/minifloat.hpp`): formato 1\|5\|10, conversiones, aritmética y matrices 2x2/3x3/4x4 contra `float`. |
| HOST-057 | [minifloat16_math](057_minifloat16_math/README.md) | `eng/core/minifloat_math.hpp`: `sqrt`/`exp`/`exp2`/`log`/`log2`/`log10`/`pow` (entero exacto)/`hypot` y trigonometría/`atan2` de `MiniFloat16` (16-bit, sin float ni libgcc) contra `std::`, más identidades de composición. |
| HOST-058 | [minifloat_fixed](058_minifloat_fixed/README.md) | `eng/retro/minifloat_fixed.hpp`: puente `MiniFloat16` ↔ `fix`/`fix88` (conversiones saturadas, producto mixto y `transform` de coordenadas fijas con matriz MF). |
| HOST-059 | [scalar_math](059_scalar_math/README.md) | Base escalar-independiente: `numeric_traits` + guards de compilación (`range_fits`/`require_division`), `interp.hpp` y `geometry.hpp` genéricos sobre `double`, `MiniFloat16` y `q12` (fixed). |
| HOST-060 | [noise](060_noise/README.md) | `eng/core/noise.hpp`: `value_noise1/2/3` y `fbm1/2/3` genéricos, ruido periódico (tileable), determinismo y rango [0,1]; MF vs `double`. |
| HOST-061 | [flat_mapper](061_flat_mapper/README.md) | `eng::field::map_flat_scroll`: mapper neutral cámara→`planeaddx`/`planeaddy`/`BPLCON1`/`BPLMOD` del virtual playfield (fetch ancho `$30`). |
| HOST-062 | [polygon_fill_sink](062_polygon_fill_sink/README.md) | `eng::field::PolygonFillSink`: seam de relleno por hardware (`Playfield::fill_polygon`), con geometría planar correcta (interleaved/contiguo) y fallback CPU. |
| HOST-063 | [ring_mapper](063_ring_mapper/README.md) | `eng::field::map_ring_scroll`: mapper neutral del corkscrew (planeaddx/BPLCON1 con fetch ancho, offset del anillo y split); fórmula extraída de `XLimitedPlayfield` con equivalente compile-time/runtime. |
| HOST-064 | [spline_easing](064_spline_easing/README.md) | `eng/core/spline.hpp` (Hermite/Catmull-Rom) y easing de `interp.hpp`: genéricos sobre `double`, `MiniFloat16` y `q12`. |
| HOST-065 | [scalar_ops](065_scalar_ops/README.md) | `eng/core/scalar_ops.hpp`, easings `_back`/`smooth_damp`/`repeat`/`pingpong` y `bezier2`/`bezier3`: genéricos sobre `double`, `MiniFloat16` y `q12`. |
| HOST-066 | [route_camera](066_route_camera/README.md) | `eng/scene/route_camera.hpp`: fases de la ruta, círculo sobre `radius_scale` (vía `eng::SineTable`), espejo `mirror_x` y modo salto dentro de límites. |
| HOST-067 | [multi_buffered](067_multi_buffered/README.md) | `scene::compose` con `SceneResources.buffers`: N buffers de display, `commit()` (publica y rota) y parcheo de `BPLxPT` sin dueño de memoria externa. |
| HOST-068 | [double_buffer_scroll](068_double_buffer_scroll/README.md) | `eng::field::DoubleBufferScrollPlayfield`: 2 bitmaps, `flip()` y `hardware_view()` del delantero. |
| HOST-069 | [copper_double_buffer](069_copper_double_buffer/README.md) | `eng::copper::DoubleBuffer`: 2 bloques de copperlist, `flip`, publicar; y `TileScrollScene` parcheando el bloque inactivo. |
| HOST-070 | [copper_plan](070_copper_plan/README.md) | `eng::copper::Plan`: orden por scanline, **prioridad `(superficie, z)`**, doble buffer de copperlist, publicación y rechazo por overflow. |
| HOST-072 | [actor](072_actor/README.md) | Sistema de objetos (`scene/actor.hpp`): almacén generacional, políticas de transparencia y fondo, anclaje/offset, geometría y emisión de los `BlitJob` del BOB (matriz por job), save-under por buffer, orden por superficie/`z`, sprites (intents, tiras, plantilla→intenciones, degradación a BOB) y necesidades de Copper ancladas con prioridad. Absorbe el antiguo HOST-071. |
| HOST-073 | [basic_type_traits](073_basic_type_traits/README.md) | `eng/core/util/type_traits.hpp` y `util.hpp`: rasgos, `move/forward/swap/exchange`, `Ref`/`cref`. |
| HOST-074 | [bit_ops](074_bit_ops/README.md) | `eng/core/util/bit.hpp`: `popcount`/`clz`/`ctz`/`rotl`/`rotr`/`bswap`/`byteswap`. |
| HOST-075 | [algorithm](075_algorithm/README.md) | `eng/core/util/algorithm.hpp`: `copy`/`fill`/`find`/`min`/`max`/`clamp` sobre `Span`. |
| HOST-076 | [array_bitset](076_array_bitset/README.md) | `eng/core/util/array.hpp` (`Array<T,N>` agregado) y `bitset.hpp` (`BitSet<N>`). |
| HOST-077 | [static_vector_ring_buffer](077_static_vector_ring_buffer/README.md) | `static_vector.hpp` (capacidad fija, sin heap) y `ring_buffer.hpp`. |
| HOST-078 | [optional_expected](078_optional_expected/README.md) | `optional.hpp` y `expected.hpp` (sin excepciones). |
| HOST-079 | [string_view_function_ref](079_string_view_function_ref/README.md) | `string_view.hpp` y `function_ref.hpp` (sin virtuals ni heap). |
| HOST-080 | [allocator_hash](080_allocator_hash/README.md) | `allocator.hpp` (`Allocator`, `Null/Bump/Inline`) y `hash.hpp`. |
| HOST-081 | [vector_small_vector](081_vector_small_vector/README.md) | `vector.hpp` y `small_vector.hpp` (crecen solo por `Allocator`). |
| HOST-082 | [flat_map_set](082_flat_map_set/README.md) | `flat_map.hpp`/`flat_set.hpp` (ordenados en array contiguo). |
| HOST-083 | [hash_map_set](083_hash_map_set/README.md) | `hash_map.hpp`/`hash_set.hpp` (abiertos, sin heap). |
| HOST-084 | [arena_chunked](084_arena_chunked/README.md) | `arena_alloc.hpp` (`ArenaAlloc`) y `chunked_vector.hpp`. |
| HOST-085 | [direct_map](085_direct_map/README.md) | `direct_map.hpp`: mapa indexado directo (`SampleBank`). |
| HOST-086 | [dynamic_hash_map](086_dynamic_hash_map/README.md) | `dynamic_hash_map.hpp`: hash con rehash en un `Allocator`. |
| HOST-087 | [intrusive_list](087_intrusive_list/README.md) | `intrusive_list.hpp`: listas intrusivas con free-list O(1) (`BackgroundQueue`). |
| HOST-088 | [pool](088_pool/README.md) | `pool.hpp`: `Pool` con handles generacionales (`ActorStore`). |
| HOST-089 | [priority_queue](089_priority_queue/README.md) | `priority_queue.hpp`: heap binario de capacidad fija. |
| HOST-090 | [sort_extra](090_sort_extra/README.md) | `eng/core/sort.hpp`: ordenación ampliada (`stable`/`nth`/`partial`/`is_sorted`/radix). |
| HOST-091 | [stack_queue_enum_set](091_stack_queue_enum_set/README.md) | `stack_queue.hpp` (`Stack`/`Queue`/`Deque`) y `enum_set.hpp` (`EnumSet`). |
| HOST-092 | [scope_guard_static_string](092_scope_guard_static_string/README.md) | `scope_guard.hpp` (`ScopeGuard`) y `static_string.hpp` (`StaticString`). |
| HOST-093 | [stats](093_stats/README.md) | `util/stats.hpp`: sum/mean/varianza/desviación (acumuladores anchos, agnóstico de escalar). |
| HOST-094 | [color](094_color/README.md) | `util/color.hpp`: conversiones y mezcla de color. |
| HOST-095 | [collision](095_collision/README.md) | `util/collision.hpp`: AABB/círculo/rect y resolución. |
| HOST-096 | [text](096_text/README.md) | `util/text.hpp`: formateo de enteros sin heap. |
| HOST-097 | [grid](097_grid/README.md) | `util/grid.hpp`: rejilla 2D indexada. |
| HOST-098 | [broadphase](098_broadphase/README.md) | `util/broadphase.hpp`: partición espacial para colisiones. |
| HOST-099 | [pathfinding](099_pathfinding/README.md) | `util/pathfinding.hpp`: A*/BFS sobre rejilla. |
| HOST-100 | [random_dist](100_random_dist/README.md) | `core/random.hpp`: distribuciones y determinismo. |
| HOST-101 | [noise_extra](101_noise_extra/README.md) | `core/noise.hpp`: ruido ampliado. |
| HOST-102 | [dsp](102_dsp/README.md) | `util/dsp.hpp`: osciladores/filtros (con `fixed_math`). |
| HOST-103 | [scalar_util](103_scalar_util/README.md) | `stats`/`dsp` y utilidades por escalar. |
| HOST-104 | [fixed_math](104_fixed_math/README.md) | `core/fixed_math.hpp`: trigonometría Fixed (tablas, precisión, tamaño configurable). |
| HOST-105 | [sprite_hrearm](105_sprite_hrearm/README.md) | Multiplexado **horizontal** de sprites (`Scheduler::emit_sprite_horizontal_rearm(s)`/`reposition` + `graphics::SpriteHorizontalRearm`): codificación AHRM de `SPRxPOS`/`SPRxCTL`, secuencia WAIT+POS+CTL+DATA+DATB sin tocar `SPRxPT`, y orden por `hpos` de la lista. |
| HOST-106 | [scroll_saveword_guard](106_scroll_saveword_guard/README.md) | `field/scroll_engine.hpp`: la costura (`save_word`) se **restaura** cuando `add_draw` rechaza el frame (ScopeGuard); en el camino correcto no se restaura. |
| HOST-107 | [goap](107_goap/README.md) | `ai/planning/goap.hpp`: planificador GOAP (hechos booleanos, acciones con coste, A*). Planes óptimos para Torres de Hanoi (7), receta de un pastel (coste 20) y misión de un soldado (coste 26), con `forbid` y casos límite. |
| HOST-108 | [state_machine](108_state_machine/README.md) | `util/state_machine.hpp`: FSM de tabla `constexpr` externa (`StateMachine<State,Event>`, `Transition`). Semáforo y FSM de IA de un guardia; evento sin transición, `reset` y orden de tabla. |
| HOST-109 | [event](109_event/README.md) | `util/event.hpp`: emisor de eventos de capacidad fija (`Event<Signature,MaxSubscribers>`) con suscriptores `FunctionRef`. Suscripción/emit/clear, capacidad y orden. |
| HOST-110 | [agent_fsm](110_agent_fsm/README.md) | `ai/decision/agent_fsm.hpp`: `AgentFsm` envuelve `util::StateMachine` y añade efectos de entrada/salida de estado; evento sin transición y reemplazo de efecto. |
| HOST-111 | [blackboard](111_blackboard/README.md) | `ai/decision/blackboard.hpp`: `Blackboard<Key,Value,MaxKeys>` (memoria compartida de la IA) con claves densas; `find` O(1), sobrescritura y valores struct. |
| HOST-112 | [utility](112_utility/README.md) | `ai/decision/utility.hpp`: `Utility` (media ponderada en [0,1000], `muls.w`/`divs.w`) y `UtilitySelector` (mejor opción, empate -> índice menor). Decisión de un guardia. |
| HOST-113 | [behavior_tree](113_behavior_tree/README.md) | `ai/decision/behavior_tree.hpp`: `BehaviorTree<MaxNodes>` sin heap (secuencia/selector, cortocircuito, `no_node` al llenarse). Guardia dispara/recarga. |
| HOST-114 | [flow_field](114_flow_field/README.md) | `ai/navigation/flow_field.hpp`: campo de flujo por Dijkstra multi-fuente (`compute_flow_field<W,H>`, `flow_next<W>`); coste uniforme, muro, región inalcanzable. |
| HOST-115 | [steering](115_steering/README.md) | `ai/steering/steering.hpp`: `seek`/`flee`/`arrive`, flocking (`separation`/`cohesion`/`alignment`/`flock`) y `pursue`/`evade`/`wander`/`avoid_circles`, genérico sobre `double` y `q12`. |
| HOST-116 | [waypoints](116_waypoints/README.md) | `ai/navigation/waypoints.hpp`: `WaypointGraph<MaxNodes,MaxEdges>` + A* sobre el grafo (heurística Manhattan, scratch del llamador). Ruta óptima, inalcanzable y capacidad. |
| HOST-117 | [perception](117_perception/README.md) | `ai/perception/influence_map.hpp` (`InfluenceMap<W,H>`: deposit/decay/strongest) y `ai/perception/agent_memory.hpp` (`AgentMemory`: see/tick/fresh/stale/forget). |
| HOST-118 | [navmesh](118_navmesh/README.md) | `ai/navigation/navmesh_lite.hpp`: `NavMesh` de polígonos convexos con portales; localizar punto, A* por adyacencia, puntos medios y string-pulling (funnel). |
| HOST-119 | [union_find](119_union_find/README.md) | `util/union_find.hpp`: `UnionFind<MaxElements>` (DSU: find/unite/connected/component_size); islas de celdas transitables. |
| HOST-120 | [sparse_set](120_sparse_set/README.md) | `util/sparse_set.hpp`: `SparseSet<T,MaxElements>` (disperso-denso, altas/bajas O(1), iteración contigua, swap-remove). |
| HOST-121 | [bitstream](121_bitstream/README.md) | `util/bitstream.hpp`: `BitWriter`/`BitReader` (campos de 1..32 bits, LSB-first) sobre un buffer. Round-trip, capacidad y fin de buffer. |
| HOST-122 | [dynamic_bitset](122_dynamic_bitset/README.md) | `util/dynamic_bitset.hpp`: `DynamicBitSet<Allocator>` (tamaño fijado en `init`, palabras en arena); máscara de la última palabra. |
| HOST-123 | [packed_level](123_packed_level/README.md) | Consumidor de `bitstream`/`dynamic_bitset`: nivel empaquetado (cabecera + 4 bits de tile + flag) con round-trip 100 %, y set de tiles sucios de 4096 celdas. |
| HOST-124 | [string_interner](124_string_interner/README.md) | `util/string_interner.hpp`: `StringInterner<MaxStrings,Allocator>` (dedup por contenido, id -> texto, arena). |
| HOST-125 | [convex_sat](125_convex_sat/README.md) | `util/collision.hpp`: `convex_overlap` (SAT 2D, polígonos convexos) y `point_in_convex`; rombo (ejes no alineados), borde y sentido de giro. |
| HOST-126 | [graph](126_graph/README.md) | `util/graph.hpp`: `Graph<MaxNodes,MaxEdges>` (adyacencia) + `graph_bfs`/`graph_astar`/`topological_sort`. Consolida waypoints (HOST-116). |
| HOST-127 | [lru_cache](127_lru_cache/README.md) | `util/lru_cache.hpp`: `LruCache<K,V,N>` (LRU O(1), get/peek/put/erase, sin heap). |
| HOST-128 | [task](128_task/README.md) | `util/task.hpp`: `TaskSequence<N>` y `Delay` (tareas *stackless*: espera, encadena y aborta). |
| HOST-129 | [interval](129_interval/README.md) | `util/interval.hpp`: `Interval` e `IntervalSet<N>` (rangos `[lo,hi)`, fusión de solapes/adyacencias, `contains` binario). |
| HOST-130 | [variant](130_variant/README.md) | `util/variant.hpp`: `Variant<Ts...>` (union etiquetada sin heap) usada como comandos heterogéneos (`index`/`holds`/`get`/`visit`/`emplace`). |
| HOST-131 | [heap_bench](131_heap_bench/README.md) | Medición R5.5: heap binario (`PriorityQueue`) vs 4-ario (comparaciones, mismo orden). Conclusión: el binario gana. |
| HOST-132 | [rotozoom](132_rotozoom/README.md) | `graphics/effects/rotozoom.hpp`: muestreo por píxel con aritmética entera (tabla de seno 16.16, identidad, rotación, zoom). |
| HOST-133 | [palette_transition](133_palette_transition/README.md) | `graphics/effects/palette_transition.hpp`: estado `num/den` (una pasada y vaivén), paleta runtime y parche base/zona en `FramePlan`. |
| HOST-134 | [raster_gradient](134_raster_gradient/README.md) | `graphics/effects/raster_gradient.hpp`: muestreo de claves (lineal/cíclico, `phase`), geometría de bandas e intenciones `PaletteLine`. |
| HOST-135 | [scalar_matrix](135_scalar_matrix/README.md) | Matriz de escalares (`double`/`float`/`MiniFloat16`/`Fixed<s16,12>`/`Fixed<s32,12>`/`Fixed<s32,24>`): error de `lerp`/`smoothstep`/`normalize` vs `double` y division/raiz de `Fixed<s32,E>` (F0/F1). |
| HOST-136 | [real_scalar](136_real_scalar/README.md) | Tipos generales de simulacion (`eng::real`/`coord`/`intw`): el mismo algoritmo se compila en RETRO16, RETRO32 y NATIVE (`tools/run/run-scalar-modes.sh`) y se compara la precision (F5). |
| HOST-137 | [parallel](137_parallel/README.md) | `engine/include/eng/parallel/parallel.hpp`: primitivas de concurrencia abstraídas (`hardware_threads`, `Thread`, `Mutex`, `Atomic`, `ConditionVariable`, `StopSource`/`StopToken`, `for_each_index`); no-ops en m68k, hilos reales en el host. |
| HOST-138 | [board_core](138_board_core/README.md) | `eng/board/core/{types,zobrist,game}.hpp`: tipos base (color, pieza, casilla 0x88, `Move`, `Score`), generador Zobrist y concepto `GameRules`. |
| HOST-139 | [board_budget](139_board_budget/README.md) | `eng/board/core/budget.hpp`: perfiles `P20`…`P1M`, selección por RAM libre y reparto de TT/libro/caché/pila. |
| HOST-140 | [chess_movegen](140_chess_movegen/README.md) | `eng/board/rules/chess/`: tablero 0x88, `make`/`unmake` con Zobrist incremental, generación legal validada por **perft** (inicial/Kiwipete/al paso/promoción) y fin de partida. |
| HOST-141 | [chess_draw_endgame](141_chess_draw_endgame/README.md) | `eng/board/rules/chess/{history,endgame}.hpp`: repetición de 3 posiciones (ventana de 50 movimientos) y finales teóricos (K vs K, K+B vs K, K+R/K+Q vs K, K+P vs K por la regla del cuadrado). |
| HOST-142 | [chess_notation](142_chess_notation/README.md) | `eng/board/rules/chess/notation.hpp`: SAN (peón, enroque, captura, promoción, desambiguación, jaque/mate) y UCI. |
| HOST-143 | [chess_search](143_chess_search/README.md) | `eng/board/search/search.hpp`: negamax + alpha-beta + iterative deepening + quiescence; mate en 1, dama colgada, recaptura, presupuesto de nodos y `StopToken`. |
| HOST-144 | [chess_tt_ordering](144_chess_tt_ordering/README.md) | `eng/board/search/tt.hpp` (entrada de 12 B, sondeo/escritura/reemplazo/reuso) y `rules/chess/ordering.hpp` (MVV-LVA y killer). |
| HOST-145 | [chess_eval](145_chess_eval/README.md) | `eng/board/eval/{chess_eval,features}.hpp`: evaluación (material+PST+movilidad+peones+desarrollo) y rasgos de desarrollo. |
| HOST-146 | [board_storage](146_board_storage/README.md) | `eng/board/storage/block_source.hpp` (`BlockSource` 3 estados y `RamBlockSource`) y `knowledge/cache.hpp` (caché LRU de bloques). |
| HOST-147 | [board_knowledge](147_board_knowledge/README.md) | `eng/board/knowledge/{book,endgame_tables}.hpp`: libro ordenado por clave, nombres, tablas de finales y round-trip byte a byte por bloque. |
| HOST-148 | [chess_b5](148_chess_b5/README.md) | `eng/board/search/{pruning,time,search}.hpp`: null-move, `TimeManager`, PV/Multi-PV y análisis paralelo determinista. |
| HOST-149 | [chess_explain](149_chess_explain/README.md) | `eng/board/explain/`: NLG por plantillas ES/EN (jaque, material, dama prematura, desarrollo, rey en el centro), tono y truncado seguro. |
| HOST-150 | [binary](150_binary/README.md) | `eng/core/util/binary.hpp`: `ByteReader`/`ByteWriter` little-endian sobre `Span` con comprobación de límites (round-trip, bounds, copia de bloques). |
| HOST-151 | [board_file](151_board_file/README.md) | `eng/board/storage/file_block_source.hpp`: E/S real de bloques desde un fichero del PC; cadena entradas→fichero→`BlockCache`→`probe_book`. |
| HOST-152 | [sim_creature](152_sim_creature/README.md) | `eng/sim/` (modelo de criatura): `types`/`needs`/`personality`/`species`/`relationship`/`tracker`/`mind`/`behavior`/`creature`. Presupuesto por plantilla, necesidades (con exposición ambiental genérica), 12 emociones, personalidad, relaciones y trackers con desalojo, y decisión por utilidad con histéresis. |
| HOST-153 | [sim_world](153_sim_world/README.md) | `eng/sim/world.hpp` + `society.hpp`: LOD abstracto/realizado, `tick_realized`/`tick_abstract` escalonado, grafo de habitaciones, migración al refugio con peligro ambiental, muerte por hambre, sociedad (reputación/facciones y `Pack`), **ciclo de vida** (edad/madurez/muerte natural), **reproducción por pareja**, **puesta de la reina** y **planificación GOAP integrada** (`replan`/`advance_plan`), y determinismo. |
| HOST-154 | [sim_mind_society](154_sim_mind_society/README.md) | `eng/sim/{knowledge,hierarchy,genetics,colony,lifecycle,relationship}.hpp` + `mind`: aprendizaje y compartición, emociones ampliadas, **afecto dirigido** (`affect`/`bond_score`/`most_loved`/`most_hated`), jerarquía/sumisión/libertad, genética y castas, **ciclo de vida** (etapas/reproducción/sesgo de casta) y estigmergia por feromonas. |
| HOST-155 | [sim_planner](155_sim_planner/README.md) | `eng/sim/planner.hpp` + `domain.hpp`: `PlannerDriver`/`PlanRunner` conectan `eng::ai::Goap` con las criaturas y **dominio de construcción** (comida→herramienta→refugio) con inventario (`SimInventory`) y acciones ejecutables (`SimActionKind`). |
| HOST-156 | [sim_body](156_sim_body/README.md) | `eng/sim/body.hpp`: cuerpo procedural — `ChainBody` (cadena + IK FABRIK, genérica sobre `double`/`q12`) y postura expresiva (`BodyPose`, `pose_from_behavior`/`pose_from_state` con afecto y vínculo). |
| HOST-157 | [sim_objects_economy](157_sim_objects_economy/README.md) | `eng/sim/{inventory,object,economy}.hpp`: inventario de pilas con etiquetas, objetos del mundo (`ItemStore`) y ejecución material de los pasos del dominio, más economía (precio por oferta/demanda) y reputación por regalos/tributos. |
| HOST-158 | [sim_terrain_climate](158_sim_terrain_climate/README.md) | `eng/sim/{terrain,climate}.hpp`: representación algorítmica del mundo (tipos de terreno, travesía por capacidades, coste/cobertura/abrigo, `TerrainMap` con `eng::util::astar`) y clima por región (`Climate`, exposición efectiva) integrado en el mundo. |
| HOST-159 | [sim_rumors](159_sim_rumors/README.md) | `eng/sim/rumor.hpp`: memoria de grupo por facción (`GroupMemory`), difusión de conocimiento entre correligionarios, y traducción a reputación colectiva y demanda económica (`apply_group_knowledge`). |
| HOST-160 | [sim_senses](160_sim_senses/README.md) | `eng/sim/senses.hpp`: percepción multimodal (visión con cono, oído que cruza regiones, olfato, tacto, gusto, temperatura), geometría sin trigonometría (sectores) y novedad/atención. |
| HOST-161 | [sim_memory](161_sim_memory/README.md) | `eng/sim/memory.hpp`: memoria de corto plazo (trackers multimodales con saliencia), olvido, **consolidación** a largo plazo (`KnowledgeSet`) y lectura (`recall`), integrado en el mundo (`sense`/`integrate_senses`/`consolidate_memory`). |
| HOST-162 | [sim_attention](162_sim_attention/README.md) | `eng/sim/senses.hpp` + `behavior.hpp`: **atención** — `focused` (el miedo estrecha/acorta la vista y agudiza el oído; la ira enfoca), `senses_from_genome` con **ecolocalización**, y `attention_score`/`best_attention_tracker` (confianza + saliencia + multimodalidad). |
| HOST-163 | [sim_spatial_memory](163_sim_spatial_memory/README.md) | `eng/sim/memory.hpp` + `world.hpp`: **memoria espacial** (recordar regiones como refugio/peligro), **rutas macro** por el grafo de regiones (`route_room`/`route_first_step`) y `preferred_refuge` que guía la migración hacia un refugio recordado. |
| HOST-164 | [sim_mental_map](164_sim_mental_map/README.md) | `eng/sim/mental_map.hpp`: el mapa mental guía el movimiento — `place_bias`, `MentalOverlay` (coste extra para `eng::util::astar`) y `deposit_mental_danger` (InfluenceMap), más olvido de lugares (`decay_places`/`forget_place`). |
| HOST-165 | [sim_climate_fronts](165_sim_climate_fronts/README.md) | `SimWorld::diffuse_climate`: el clima se **propaga** entre regiones vecinas (frentes) conservando el `HazardKind`, avanza por el grafo, no salta a regiones aisladas y se disipa. |
| HOST-166 | [sim_world_mental_routes](166_sim_world_mental_routes/README.md) | `SimWorld` expone el mapa mental de una criatura: `mental_bias`, `stamp_mental_overlay` (coste para `eng::util::astar`) y `stamp_mental_danger` (InfluenceMap) para la ruta fina y el flujo. |
| HOST-167 | [sim_dynamic_terrain](167_sim_dynamic_terrain/README.md) | `SimWorld::apply_terrain_event` + `terrain.hpp`: terreno **dinámico** (inundación, incendio, derrumbe, regeneración), su efecto en la travesía y su interacción con el clima, parametrizable. |
| HOST-168 | [sim_trade](168_sim_trade/README.md) | `eng/sim/economy.hpp`: **trueque** — `trade_value`, `bargain_score` (valor + necesidad), `execute_trade` y `SimWorld::offer_trade` con efecto en la reputación. |
| HOST-169 | [sim_biome](169_sim_biome/README.md) | `eng/sim/biome.hpp`: **biomas/ecosistemas** — perfiles (terreno, clima típico, recursos), `species_fits_biome` y `SimWorld::set_biome`/`apply_biome` que vuelcan el bioma en la región. |
| HOST-170 | [sim_communication](170_sim_communication/README.md) | `eng/sim/communication.hpp`: **lenguaje y gestos** — `make_signal`/`receive_signals`/`apply_signal_effect` y `SimWorld::broadcast_signals`, con señales atadas a conducta, emoción y jerarquía. |
| HOST-171 | [sim_culture](171_sim_culture/README.md) | `eng/sim/culture.hpp`: **cultura y rituales** — `learn_ritual`/`ritual_for_event`/`perform_ritual`, transmisión por enseñanza y actuación en el mundo (`enact_ritual`). |
| HOST-172 | [sim_pack](172_sim_pack/README.md) | `eng/sim/pack.hpp`: **coordinación de manadas** — roles (`PackRole`), flanqueo (`flank_goal`) y `SimWorld::coordinate_packs` (el líder orienta a los miembros a envolver la presa). |
| HOST-173 | [sim_scenarios](173_sim_scenarios/README.md) | **Laboratorio de escenarios**: simulaciones largas (abundante/escaso/depredadores/manada) con digesto de población, nacimientos, muertes por causa y conductas; invariantes suaves de "mundo vivo". |
| HOST-174 | [sim_player_lod](174_sim_player_lod/README.md) | `eng/sim/lod.hpp` + `season.hpp` + `avatar.hpp`: **LOD** por distancia (realized/abstract/dormant) con **despertar gradual** (`lod_blend`, `wake_per_frame`), **aforo dinámico** por estación y clima, y **jugador simulado** (intención, movimiento/comida y resumen de percepción/carga). |
| HOST-175 | [sim_player_input](175_sim_player_input/README.md) | `eng/sim/avatar.hpp`: **entrada humana** (`PlayerInput`/`player_control`) — el humano sustituye a la IA sin tocar el mundo: movimiento con recorte, interacción donde hay recurso, descanso y paridad IA/humano. |
| HOST-176 | [or_blob_batch](176_or_blob_batch/README.md) | `eng/platform/amiga/blob.hpp`: `OrBlobBatch` — secuencia de registros del lote de BOBs OR intercalado de `bobs3d` (`begin/one/end`), host-testable por inyección de la base de registros. |
| HOST-177 | [scalar_trig_generic](177_scalar_trig_generic/README.md) | Trig escalar genérica: el mismo algoritmo `rotate<S>` con `float` y con `q12` (ángulo en radianes), `scalar_sin`/`scalar_cos`/`scalar_sincos` y la especialización retro exacta (`eng/retro/fixed_trig.hpp`, `kSinTab`). |
| HOST-178 | [dna_helix](178_dna_helix/README.md) | Port de `GenCircularDoubleHelix` de `dna3d` a `Turns`/`sin`/`cos`, verificado punto a punto contra la transcripción del original (`kSinTab`) e invariantes de geometría. |
| HOST-179 | [go_rules](179_go_rules/README.md) | `eng/board/rules/go/`: tablero 9×9, grupos/libertades, captura, suicidio y ko simple; jugada con flag de captura. |
| HOST-180 | [go_search](180_go_search/README.md) | `eng/board/eval/go_eval.hpp` + `rules/go/rules.hpp`: territorio/capturas/atari, `GoSearcher` (negamax/αβ/TT) encuentra la captura. |
| HOST-181 | [chess_variants](181_chess_variants/README.md) | `rules/chess/variant.hpp` (Chess960: 960 disposiciones y enroque generalizado) y `tournament.hpp` (torneos rápidos con presupuesto de nodos). |
| HOST-182 | [go_complete](182_go_complete/README.md) | Go: pase/dos pases (`GameEnded`), superko (historial de claves) y apertura (`knowledge/patterns.hpp`). |
| HOST-183 | [chess_variants_cond](183_chess_variants_cond/README.md) | Ajedrez: variantes de condición King of the Hill y Three-check vía `variant_score`. |
| HOST-184 | [chess_knowledge](184_chess_knowledge/README.md) | Ajedrez: sonda del libro de aperturas (`opening.hpp`) y finales teóricos en la evaluación. |
| HOST-185 | [goap_numeric](185_goap_numeric/README.md) | `ai/planning/numeric_goap.hpp`: GOAP con variables numéricas cuantizadas (enteros y decimales), saturación y caché (memo de planes + sufijo). |
| HOST-186 | [goap_numeric_relaxed](186_goap_numeric_relaxed/README.md) | GOAP numérico con heurística relajada (h_max) y memo de heurística entre llamadas. |
| HOST-187 | [pgn](187_pgn/README.md) | `rules/chess/pgn.hpp` (escritor PGN sin heap ni I/O) y `rules/chess/opening_book.hpp` (líneas de apertura incorporadas que comparten demo y selfplay). |
| HOST-188 | [cards_core](188_cards_core/README.md) | `eng/cards/core/{types,deck,budget}.hpp`: tipos de carta, baraja determinista (mismo PRNG+semilla) y presupuesto de naipes `N20`…`N512`. |
| HOST-189 | [cards_hand_rank](189_cards_hand_rank/README.md) | `eng/cards/rules/hand_rank.hpp`: evaluador de 5/7 cartas por conteo, categorías, orden total, escalera de as bajo y kickers. |
| HOST-190 | [cards_holdem](190_cards_holdem/README.md) | `eng/cards/rules/texas_holdem.hpp`: reparto/ciegas, calles, acciones legales, resolución por retirada y botes laterales. |
| HOST-191 | [cards_equity](191_cards_equity/README.md) | `eng/cards/eval/equity.hpp`: equity Monte Carlo determinista, heurística preflop y pot odds. |
| HOST-192 | [cards_selfplay](192_cards_selfplay/README.md) | `eng/cards/{ai/bot,sim/session}.hpp`: estilos, modelo de rival, sesiones CPU vs CPU, conservación y determinismo. |
| HOST-193 | [cards_range](193_cards_range/README.md) | `eng/cards/eval/range.hpp`: 169 clases de mano inicial, `HandRange`, equity contra rango y tabla preflop (`build_preflop_table`). |
| HOST-194 | [cards_variants](194_cards_variants/README.md) | `eng/cards/rules/variants.hpp` + `evaluate_omaha`: Omaha (2 hole + 3 board), reparto por variante y estructura Limit (apuesta fija y tope de subidas). |
| HOST-195 | [cards_wildcards](195_cards_wildcards/README.md) | Comodines: mazo de 54 cartas, sustitución por la mejor carta en `evaluate_hand`, heurística preflop y showdown con comodín. |
| HOST-196 | [cards_omaha](196_cards_omaha/README.md) | Omaha de extremo a extremo: `equity_vs_random_omaha` (4 cartas) y `run_session` con `variant=Omaha` (conservación, determinismo y comodines). |
| HOST-197 | [cards_stud](197_cards_stud/README.md) | `eng/cards/rules/seven_stud.hpp`: Seven-Card Stud (ante, bring-in, 5 calles, showdown y retirada). |
| HOST-198 | [cards_five_draw](198_cards_five_draw/README.md) | `eng/cards/rules/five_draw.hpp` + `evaluate_deuces_wild`: Five-Card Draw con descarte y Deuces Wild. |
| HOST-199 | [sim_persona](199_sim_persona/README.md) | `eng/sim/{psyche_traits,archetypes,persona}.hpp`: rasgos de psique, aptitudes, defectos, catálogo de ~39 arquetipos y `materialize` con jitter determinista. |
| HOST-200 | [sim_expression](200_sim_expression/README.md) | `eng/sim/expression.hpp`: canales y gestos, control por gesto, compostura efectiva y cálculo de la fuga (autonómicos vs volitivos, microexpresiones). |
| HOST-201 | [sim_read](201_sim_read/README.md) | `eng/sim/read.hpp`: lectura de tells (aprendizaje en showdown, Bayes-lite, prior por arquetipo y suspicacia; pardillo vs listillo). |
| HOST-202 | [sim_psyche](202_sim_psyche/README.md) | `eng/sim/psyche.hpp`: estado temporal (tilt, confianza, racha, fatiga) y evolución por eventos de mesa. |
| HOST-203 | [sim_convention](203_sim_convention/README.md) | `eng/sim/convention.hpp`: convenciones secretas (pacto gesto→señal, disimulo, exposición e inferencia) — base del Mus. |
| HOST-204 | [cards_persona](204_cards_persona/README.md) | `eng/cards/ai/persona_bot.hpp`: parámetros del bot por arquetipo/estado, emisión de tells, lectura de rivales y rango combinado con tells. |
| HOST-205 | [sim_input_expression](205_sim_input_expression/README.md) | `eng/sim/expression.hpp::expression_from_input`: el humano como personaje (gestos explícitos + timing) y lectura simétrica por la mesa. |
| HOST-206 | [sim_introspection](206_sim_introspection/README.md) | `eng/sim/introspection.hpp` (afecto desde los hechos del motor) + puente `eng/board/persona.hpp`; y `gestures_for_pace` (impaciencia/bostezo si el humano tarda). |
| HOST-207 | [math_expr](207_math_expr/README.md) | `eng/core/expr.hpp`: expression templates lite (árbol en compilación, evaluación única, `converter` para `Fixed`, fusión por componente de `Vec`/`Mat`). |
| HOST-211 | [mesh_poly_render](211_mesh_poly_render/README.md) | `eng/graphics/mesh_renderer.hpp::mesh_render_poly_filled`: malla n-gon (transform + culling por normal + `Surface::fill_polygon`) con `project_perspective` genérica sobre el escalar. |
| HOST-212 | [canvas_scene](212_canvas_scene/README.md) | `eng/graphics/composition/compose.hpp`: `field::Surface` sobre escena en layout **interleaved** y **contiguo** (`ContiguousPlayfield`): `fill_polygon`/`draw_line`/`blit` y doble buffer. |
| HOST-213 | [outline_xor](213_outline_xor/README.md) | `eng/retro/flat_shade_xor.hpp`: técnica Amiga de contorno EOR + área fill XOR (secuencia de Blitter, horizontales descartadas, un fill). |
| HOST-214 | [copper_patch](214_copper_patch/README.md) | `eng/copper::PatchHandle` (`scheduler.hpp`): MOVE parcheable por frame (precisión quirúrgica en el modelo de composición de escenas). |
| HOST-215 | [scene_compose](215_scene_compose/README.md) | `eng/graphics/composition/compose.hpp`: escena planar por etapas (display + paleta + etapa propia con `PatchHandle`); presets en vez de clases por driver. |
| HOST-216 | [scene_display_limits](216_scene_display_limits/README.md) | `eng/graphics/composition/limits.hpp`: perfiles OCS/ECS/AGA, validación `validate`/`valid_scene` y coste de bus `dma_cost` (fetch 1×/2×/4×, slots 226/27). |
| HOST-217 | [polygon_planes](217_polygon_planes/README.md) | `eng/graphics/polygon_planes.hpp`: relleno de polígonos **compuesto por bitplane** (CPU): un fill por plano según los bits del color, con cancelación de aristas compartidas (even-odd). |
| HOST-218 | [seam_c2p](218_seam_c2p/README.md) | `Rasterizer::c2p` (seam): chunky→planar unificado (CPU vía `c2p_1x1_4`/`naive`), comparado con la referencia. |
| HOST-219 | [os_core](219_os_core/README.md) | Mini-SO núcleo (`eng/os/message.hpp` + `port.hpp`): `Msg` trivial, `MsgQueue` SPSC (FIFO/peek/overflow) y señales OR-eadas. |
| HOST-220 | [ui_bridge](220_ui_bridge/README.md) | Mini-SO: puente `os::Msg` → `ui::UiEvent` (ratón, teclado con modificadores, joystick/pad; descarta lo que no es entrada). |
| HOST-222 | [os_timer](222_os_timer/README.md) | Mini-SO: `TimerService` (frames/µs, one-shot/periódico, `stop`, capacidad). |
| HOST-223 | [ui_painter](223_ui_painter/README.md) | GUI G0: `eng::ui::UiPainter`/`UiTheme` (chrome: `fill`/`frame`/`bevel_out`/`bevel_in`/`panel`/`button_face`/glifo), medida y recorte de texto (`text_width`/`draw_text_clipped`) y `eng::ui::Rect` = `eng::Box`. |
| HOST-224 | [ui_widgets](224_ui_widgets/README.md) | GUI G1: árbol de widgets (`eng/ui/widget.hpp`), `DirtyList<Max>` (fusión/desborde) y `Panel`/`Label` con `draw_widget` (`switch` exhaustivo) y `measure`. |
| HOST-225 | [ui_context](225_ui_context/README.md) | GUI G2: `eng::ui::UiContext` (hit-test de delante hacia atrás, foco, despacho) y `Button` (`on_click` al soltar dentro, una vez). |
| HOST-226 | [ui_toggle](226_ui_toggle/README.md) | GUI G3: `CheckBox` (alterna `*value` al soltar dentro) y `RadioButton` (activa uno y desactiva el grupo por `group_id`). |
| HOST-227 | [ui_edit](227_ui_edit/README.md) | GUI G4: foco de teclado (`Tab`/`Shift+Tab`) y `EditBox` (buffer externo, insertar/borrar, caret y vista horizontal). |
| HOST-228 | [ui_layout](228_ui_layout/README.md) | GUI G5: `layout_stack_v`/`layout_stack_h`, `anchor`, `measure` de botón y cambio de tema (recolorea). |
| HOST-229 | [ui_windows](229_ui_windows/README.md) | GUI G6: ventanas (`Window`/`Popup`/`Toast`/`Dialog`), Z/`raise`, modalidad, `Esc`, TTL y popup que cierra al pulsar fuera. |
| HOST-230 | [ui_compositor](230_ui_compositor/README.md) | GUI G7: compositor con `WindowBacking` (componer de atrás hacia delante, mover/redimensionar sin repintar vecinas, pool con cota). |
| HOST-231 | [box](231_box/README.md) | `eng::Box` (rect 16 bits) + adaptadores a/desde `SurfaceRect`/`ClipRect`/`DirtyRect` (round-trip y bordes). |
| HOST-232 | [draw_target](232_draw_target/README.md) | `field::DrawTarget` (Surface+Rasterizer+FramePlan+clip): `fill`/`line`/`frame`/`c2p` y `box()`. |
| HOST-233 | [api_facade](233_api_facade/README.md) | Fachada pública `eng/api/api.hpp`: un solo include expone la API estable (Box, FramePlan, raster, entrada, tareas, paleta, `GameModule`). |
| HOST-234 | [app_screen](234_app_screen/README.md) | Fachada de juego `eng::App` + `eng::Screen` (`eng/api/game.hpp`): bucle + contexto de dibujo sin exponer backend/`FramePlan`. |
| HOST-235 | [hw_info](235_hw_info/README.md) | Inventario de hardware `eng::hw` (`eng/hw/info.hpp`): consultas de capacidad, display, nombres y heurísticas (modelo, RAM, RTC). |
| HOST-236 | [os_latched](236_os_latched/README.md) | Mini-SO: prioridad (`PrioMsgQueue`, los `High` se cuelan), `peek`, coalescing de `MouseMove` y VBlank latched (secuencia + `missed`). |
| HOST-237 | [os_dispatch](237_os_dispatch/README.md) | Mini-SO: despacho por tabla (`HandlerTable`) indexada por `MsgType`, cobertura y `dispatch_all`. |
| HOST-238 | [os_time](238_os_time/README.md) | Mini-SO: `eng/os/time.hpp` — conversiones ticks↔µs (PAL/NTSC) y `ScopedTimer` con `TickSource`. |
| HOST-242 | [pcm_codec](242_pcm_codec/README.md) | Codec PCM Delta + RLE (`eng/audio/pcm_codec.hpp`): round-trip byte a byte, ratio y rechazos (codec/destino/truncado). |
| HOST-249 | [crowd](249_crowd/README.md) | Crowd genérico (`eng/ai/steering/crowd.hpp`): separación/evasión con fase amplia como política (`SpatialHash`, no `O(N²)`), probado con `s32` y `float`. |
| HOST-250 | [os_port](250_os_port/README.md) | Puerto de mensajes del mini-SO (`eng/os/port.hpp`): anillo SPSC (`push_isr`/`pop`/`peek`/`overflows`) y `MsgPort`. |
| HOST-251 | [reactive_loop](251_reactive_loop/README.md) | Bucle reactivo sobre `eng::App`: hook de VBlank del `Engine` → `MsgType::VBlank`, consumo en `update`, blit asíncrono → `MsgType::BlitDone`, `vblank_count`/`blitdone_count`, `pump`. |
| HOST-252 | [os_input](252_os_input/README.md) | Mini-SO: productores de entrada (`eng/os/input.hpp`) que emiten joystick/gamepad/ratón **solo al cambiar**, con posición clampada. |
| HOST-253 | [os_pump](253_os_pump/README.md) | Mini-SO: bucle reactivo `MessagePumpGame` (drena el puerto, `on_frame`/`on_render`). |
| HOST-254 | [asset_cache](254_asset_cache/README.md) | Recursos: `AssetCache` (`eng/res/asset_cache.hpp`) — ciclo, presupuesto, desalojo por prioridad/LRU y `pin`/`refcount`. |
| HOST-255 | [io_route](255_io_route/README.md) | Recursos: `IoUser` (cookie) y `route_io` (`eng/res/resources.hpp`) — enrutado de `FileDone`/`FileError` por `tag` sin cruzar consumidores. |
| HOST-256 | [os_keyboard](256_os_keyboard/README.md) | Mini-SO: teclado (`eng/os/input.hpp`) — bit-reverse del scancode de la CIA, down/up y modificadores. |
| HOST-257 | [os_stream](257_os_stream/README.md) | Mini-SO: `ChunkStream` (`eng/os/stream.hpp`) — doble buffer, underrun y EOF. |
| HOST-248 | [dynloader](248_dynloader/README.md) | Recursos: `DynLoader` (`eng/res/dynloader.hpp`) — `.englib` relocatable (relocaciones + símbolos). |
| HOST-258 | [hunk_loader](258_hunk_loader/README.md) | Recursos: cargador **HUNK** (`eng/res/hunk.hpp`) — segmentos en `LinearArena`, relocaciones (32/32SHORT) y símbolos; detección de formato `.englib`/HUNK en `DynLoader`. |
| HOST-259 | [floppy_mfm](259_floppy_mfm/README.md) | Disquete: decodificación **MFM** (`eng/os/floppy.hpp`) — `mfm_decode_long` inverso y `floppy_find_sector` sobre una pista AmigaDOS sintética (encoder = el del emulador). |
| HOST-261 | [ui_msg_input](261_ui_msg_input/README.md) | GUI: entrada por **mensajes** (`os::Msg` → `ui_bridge` → `UiContext`) con `keymap` rawkey Amiga → tecla lógica. |
| HOST-262 | [ui_slider](262_ui_slider/README.md) | GUI: `Slider` (click/arrastre → valor, flechas con foco, pista + pomo). |
| HOST-263 | [ui_keymap_layouts](263_ui_keymap_layouts/README.md) | GUI: keymaps nacionales (ES/FR/IT/DE/RU) — `rawkey_to_key(raw, shift, layout)`. |
| HOST-264 | [font_cyrillic](264_font_cyrillic/README.md) | Fuente: glifos cirílicos (U+0410–U+044F, Ё/ё) en `Font8` + UTF-8. |
| HOST-265 | [ui_deadkeys](265_ui_deadkeys/README.md) | GUI: teclas muertas (Alt+acento compone con la letra siguiente). |
| HOST-266 | [rect_fill_sink](266_rect_fill_sink/README.md) | Relleno de rect por hardware: `RectFillSink` + `fill_rect_hw` + elección del `BlitterRaster`. |
| HOST-267 | [ui_compositor_blit](267_ui_compositor_blit/README.md) | Compositor por `Surface::blit` (`present_blit`): equivalencia con el copiado por píxel. |
| HOST-260 | [copper_blitter](260_copper_blitter/README.md) | Copper lanza blits (Técnica A): `CopperIntentKind::BlitterJob` (`BLTCON*`/punteros/módulos/`BLTSIZE`) y **ventana segura** (`set_blitter_window`) que lo serializa con los blits de CPU. |
| HOST-208 | [ptr](208_ptr/README.md) | `eng/core/ptr.hpp`: punteros "inteligentes" sin heap (`Ref` observador no propietario y anulable, `NonNull`, `Opt` opcional en sitio). |
