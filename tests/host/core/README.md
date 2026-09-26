# Tests HOST — core

Categoría `core` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-000 | [eng_core_math](000_eng_core_math/README.md) | `eng::core::isqrt`, `eng::core::quick_sort`, `eng::core::sort_items`, `Span` (port de `libmisc` de `demoscene-repo-orig`). |
| HOST-018 | [rtc](018_rtc/README.md) | `eng::time::from_tod`: reloj de tiempo real desde el contador TOD de la CIA-A (50/60 Hz, wrap 24 h). |
| HOST-020 | [sinetable](020_sinetable/README.md) | Tabla de seno 4.12 **exacta** del original (`libmisc/sintab.c`) materializada en `eng/core/math/sinetable.hpp`; invariantes, simetría, checksum y uso por `eng/retro::sin_q12`. |
| HOST-021 | [plasma_tables](021_plasma_tables/README.md) | Datos del plasma: tablas `tab1/2/3` (`fx4i`·`SIN/COS` verbatim) y paleta 256×RGB12, con checksums frente al original. |
| HOST-022 | [fast_div](022_fast_div/README.md) | `eng::fast_div`/`is_pow2`/`ilog2`/`asr_floor`: división/módulo por potencia de dos sin el libcall `__udivsi3` del 68000. |
| HOST-040 | [internal_types](040_internal_types/README.md) | `eng/core/types/typed.hpp`: vistas con tag de dominio (`Bytes`/`Words`), unidades fuertes y bases; dominios no intercambiables (fallo de compilación). |
| HOST-041 | [block](041_block/README.md) | `Block<Tag>`: reserva de arena tipada (`LinearArena::allocate_block`, `MemoryBlock::block`); dominios no intercambiables. |
| HOST-048 | [math_fixed](048_math_fixed/README.md) | Escalar fixed-point genérico `eng::math::Fixed` (exponentes, promoción, no-mezcla, dot fusionado). |
| HOST-049 | [math_linalg](049_math_linalg/README.md) | Álgebra lineal genérica `eng::math::Vec`/`Mat`/`Affine` sobre cualquier escalar. |
| HOST-052 | [math_generic_scalar](052_math_generic_scalar/README.md) | La librería es agnóstica del escalar: 4.12, 8.8, `float` y un complejo de usuario. |
| HOST-054 | [light_ops](054_light_ops/README.md) | Sombreado por cara `eng::math::light_ops` e `hi16` contra referencia sin signo. |
| HOST-055 | [inverse_rigid](055_inverse_rigid/README.md) | `math3d::inverse_rigid`: inversa de una transformación rígida (`mT`, `-mT·t`) contra la identidad. |
| HOST-056 | [minifloat16](056_minifloat16/README.md) | `eng::math::MiniFloat16` (`eng/core/math/minifloat.hpp`): formato 1\|5\|10, conversiones, aritmética y matrices 2x2/3x3/4x4 contra `float`. |
| HOST-057 | [minifloat16_math](057_minifloat16_math/README.md) | `eng/core/math/minifloat_math.hpp`: `sqrt`/`exp`/`exp2`/`log`/`log2`/`log10`/`pow` (entero exacto)/`hypot` y trigonometría/`atan2` de `MiniFloat16` (16-bit, sin float ni libgcc) contra `std::`, más identidades de composición. |
| HOST-058 | [minifloat_fixed](058_minifloat_fixed/README.md) | `eng/retro/minifloat_fixed.hpp`: puente `MiniFloat16` ↔ `fix`/`fix88` (conversiones saturadas, producto mixto y `transform` de coordenadas fijas con matriz MF). |
| HOST-059 | [scalar_math](059_scalar_math/README.md) | Base escalar-independiente: `numeric_traits` + guards de compilación (`range_fits`/`require_division`), `interp.hpp` y `geometry.hpp` genéricos sobre `double`, `MiniFloat16` y `q12` (fixed). |
| HOST-060 | [noise](060_noise/README.md) | `eng/core/math/noise.hpp`: `value_noise1/2/3` y `fbm1/2/3` genéricos, ruido periódico (tileable), determinismo y rango [0,1]; MF vs `double`. |
| HOST-064 | [spline_easing](064_spline_easing/README.md) | `eng/core/math/spline.hpp` (Hermite/Catmull-Rom) y easing de `interp.hpp`: genéricos sobre `double`, `MiniFloat16` y `q12`. |
| HOST-065 | [scalar_ops](065_scalar_ops/README.md) | `eng/core/math/scalar_ops.hpp`, easings `_back`/`smooth_damp`/`repeat`/`pingpong` y `bezier2`/`bezier3`: genéricos sobre `double`, `MiniFloat16` y `q12`. |
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
| HOST-090 | [sort_extra](090_sort_extra/README.md) | `eng/core/data/sort.hpp`: ordenación ampliada (`stable`/`nth`/`partial`/`is_sorted`/radix). |
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
| HOST-109 | [event](109_event/README.md) | `util/event.hpp`: emisor de eventos de capacidad fija (`Event<Signature,MaxSubscribers>`) con suscriptores `FunctionRef`. Suscripción/emit/clear, capacidad y orden. |
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
| HOST-135 | [scalar_matrix](135_scalar_matrix/README.md) | Matriz de escalares (`double`/`float`/`MiniFloat16`/`Fixed<s16,12>`/`Fixed<s32,12>`/`Fixed<s32,24>`): error de `lerp`/`smoothstep`/`normalize` vs `double` y division/raiz de `Fixed<s32,E>` (F0/F1). |
| HOST-136 | [real_scalar](136_real_scalar/README.md) | Tipos generales de simulacion (`eng::real`/`coord`/`intw`): el mismo algoritmo se compila en RETRO16, RETRO32 y NATIVE (`tools/run/run-scalar-modes.sh`) y se compara la precision (F5). |
| HOST-150 | [binary](150_binary/README.md) | `eng/core/util/binary.hpp`: `ByteReader`/`ByteWriter` little-endian sobre `Span` con comprobación de límites (round-trip, bounds, copia de bloques). |
| HOST-177 | [scalar_trig_generic](177_scalar_trig_generic/README.md) | Trig escalar genérica: el mismo algoritmo `rotate<S>` con `float` y con `q12` (ángulo en radianes), `scalar_sin`/`scalar_cos`/`scalar_sincos` y la especialización retro exacta (`eng/retro/fixed_trig.hpp`, `kSinTab`). |
| HOST-207 | [math_expr](207_math_expr/README.md) | `eng/core/math/expr.hpp`: expression templates lite (árbol en compilación, evaluación única, `converter` para `Fixed`, fusión por componente de `Vec`/`Mat`). |
| HOST-208 | [ptr](208_ptr/README.md) | `eng/core/types/ptr.hpp` y `span.hpp`: punteros/vistas inteligentes sin heap (`Ref`/`NonNull`/`Opt`; `Span` con CTAD `{ptr,n}`, array sin count y conversión `Span<T>`→`Span<const T>`). |
| HOST-231 | [box](231_box/README.md) | `eng::Box` (rect 16 bits) + adaptadores a/desde `SurfaceRect`/`ClipRect`/`DirtyRect` (round-trip y bordes). |
| HOST-233 | [api_facade](233_api_facade/README.md) | Fachada pública `eng/api/api.hpp`: un solo include expone la API estable (Box, FramePlan, raster, entrada, tareas, paleta, `GameModule`). |
| HOST-333 | [expected](333_expected/README.md) | `eng/core/types/result.hpp`: `eng::Expected<T>` (valor o `eng::Result`) — idioma de error sin excepciones: `ok`/`status`, `value`/`value_or`. |
| HOST-334 | [device_single](334_device_single/README.md) | `eng/api/device.hpp`: ergonomía de un solo elemento (`Device::blitter_or_bobs(const OrBob&, …)`) — evita envolver `Span` a mano (y no se añade ctor implícito a `Span`). |
| HOST-339 | [copper_builder](339_copper_builder/README.md) | `eng/api/copper.hpp`: fachada de Copper de alto nivel (`wait_line`/`set_color`/`set_palette`/`set_scroll`) — para `ICopper` externo. |
| HOST-341 | [adapter](341_adapter/README.md) | Prueba de decisión §7.6: un consumidor implementa sus `I*` (externas) con solo `eng/api/api.hpp` + helpers (`ChipPool`/`Copper`), sin tocar `field`/`BobTarget`/registros. |
