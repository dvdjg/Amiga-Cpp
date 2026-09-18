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
| HOST-001 | [graphics_driver_contract](001_graphics_driver_contract/README.md) | Conceptos `eng::DisplayDriver` y `eng::GraphicsDriver` del ciclo de instalación del display (takeover + install): validación compile-time con `static_assert` sobre `StaticEhbScene`, `TileScrollScene`, `XlimitedScene` y compositores DPF. |
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
| HOST-016 | [ham_scene](016_ham_scene/README.md) | `eng::graphics::drivers::HamScene`: display planar con repetición de filas (cuadruplicado) — geometría de la copperlist y parametricidad. |
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
| HOST-067 | [multi_buffered](067_multi_buffered/README.md) | `eng::graphics::MultiBuffered<Driver,N>`: N buffers de display, `commit()` (publica y rota) y `bind()` sin dueño de memoria. |
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
| HOST-115 | [steering](115_steering/README.md) | `ai/steering/steering.hpp`: `seek`/`flee`/`arrive` y flocking (`separation`/`cohesion`/`alignment`/`flock`), genérico sobre `double` y `q12`. |
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
