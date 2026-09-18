# Librería de plantillas de utilidades (`eng::util`)

`engine/include/eng/core/util/` reúne las utilidades genéricas del engine que en un
programa hosted vendrían de la STL y que el runtime Amiga **no puede usar**: el cruce
es freestanding (`-nostdlib`, sin `libstdc++`, sin excepciones, sin RTTI, sin heap
durante el gameplay). No es una reimplementación completa de la librería estándar:
es una selección mínima de piezas que se usan de verdad y que se han elegido por su
relación valor/coste en un 68000.

Todas las cabeceras son **header-only**, `constexpr`, sin `malloc` (la memoria crece
solo a través de un `Allocator` que se le pase) y sin virtuals. El vocabulario vive en
el espacio de nombres `eng::util`; las matemáticas de escalares (`Fixed`,
`MiniFloat16`, `lerp`…) siguen en `eng::math`.

## 1. Encaje con lo que ya existe

La librería **complementa** el núcleo de `eng/core/`, no lo duplica:

```
   eng/core/                         eng/core/util/  (esta librería)
   ─────────────                     ─────────────────────────────
   types.hpp    tipos base           type_traits.hpp   rasgos
   span.hpp     Span<T>      ◄────── algorithm.hpp     algoritmos sobre Span
   sort.hpp     quick_sort           util.hpp          move/forward/swap/exchange
   fast_div.hpp is_pow2/ilog2 ◄───── bit.hpp           popcount/clz/rotl/bswap
   ct_array.hpp tabla por functor    array.hpp         Array<T,N> (agregado)
   typed.hpp    vistas de dominio    bitset.hpp        BitSet<N>
   word.hpp     mulu16        ◄────── hash.hpp          hash_u8/16/32, Hash<T>
   memory/arena.hpp LinearArena ◄─── arena_alloc.hpp   ArenaAlloc
                                     allocator.hpp     Allocator, Null/Bump/Inline
                                     static_vector.hpp StaticVector<T,N> (fijo)
                                     small_vector.hpp  SmallVector<T,N,A> (inline+arena)
                                     vector.hpp        Vector<T,A>        (arena)
                                     chunked_vector.hpp ChunkedVector<...> (estable)
                                     ring_buffer.hpp   RingBuffer<T,N> (doble)
                                     stack_queue.hpp   Stack/Queue/Deque<T,N>
                                     enum_set.hpp      EnumSet<E,N>
                                     stats.hpp         sum/mean/variance/median/histogram
                                     color.hpp         RGB444 (lerp/scale/HSV)
                                     collision.hpp     AABB/segmento/triángulo/círculo
                                     text.hpp          trim/split/parse/to_chars/join
                                     grid.hpp          tile/iso/hex
                                     broadphase.hpp    SpatialHash (rejilla)
                                     pathfinding.hpp   bfs/astar/reconstruct
                                     dsp.hpp           Adsr/OnePole/DelayLine/osciladores
                                     pool.hpp          Pool<T,N> (handles)
                                     priority_queue.hpp PriorityQueue<T,N,Cmp>
                                     intrusive_list.hpp IntrusiveList/SList<T>
                                     flat_map.hpp      FlatMap<K,V,N>
                                     flat_set.hpp      FlatSet<T,N>
                                     hash_map.hpp      HashMap<K,V,N>
                                     hash_set.hpp      HashSet<T,N>
                                     dynamic_hash_map.hpp DynamicHashMap<K,V,A>
                                     direct_map.hpp    DirectMap<V,N>
                                     optional.hpp      Optional<T>
                                     expected.hpp      Expected<T,E>
                                     string_view.hpp   StringView
                                     static_string.hpp StaticString<N>
                                     scope_guard.hpp   ScopeGuard
                                     function_ref.hpp  FunctionRef<Sig>
```

Puntos de reutilización explícitos:

- `has_single_bit` se apoya en `eng::is_pow2` (`fast_div.hpp`); no se redefine la
  detección de potencia de dos.
- El orden de elementos lo cubre `eng/core/sort.hpp` (`quick_sort`/`sort_items`, más `stable_sort`, `nth_element`, `partial_sort` e `is_sorted`; `radix_sort_u16` para claves de 16 bits); `algorithm.hpp` no incluye una segunda ordenación.
- `Array<T,N>` y `ct_array<T,N>` son distintos a propósito: el primero es un agregado
  que se inicializa con llaves como un array C; el segundo genera su contenido en
  compilación desde un functor.
- El crecimiento va por el concepto `Allocator` (`allocator.hpp`): `BumpAlloc`/
  `InlineAlloc`/`NullAlloc` y, sobre el modelo de memoria del engine, `ArenaAlloc`
  (`arena_alloc.hpp`) adapta `eng::LinearArena`. No hay `malloc`.
- `hash.hpp` se apoya en `eng::math::mulu16` (`word.hpp`, un `mulu.w`) y en `rotl`
  (`bit.hpp`); evita la multiplicación de 32×32 que emitiría `__mulsi3`.
- Los contenedores de capacidad fija siguen el patrón de handles/pool de `eng/task/background.hpp` (sin heap, con `valid()` explícito donde aplica).
- `eng/scene/actor.hpp` usa `eng::util::Pool<Actor, MaxActors>` como parque de actores con handles generacionales (`ActorStore`), `BitSet<MaxActors>` y `StaticVector` en la emisión de BOB.
- `eng/assets/uaf.hpp` (`Blob`) indexa los chunks con `FlatMap<ChunkType, u16, kMaxChunks>` y guarda la lista en `StaticVector<ChunkRef, kMaxChunks>`; lo consumen las demos de assets (078/100/101).
- `eng/task/background.hpp` usa `IntrusiveSList<Entry>` como free-list de slots de tarea (reparto y devolución `O(1)`, sin heap); lo ejercita la demo `081_background_tasks`.
- `eng/field/chunk_cache.hpp` indexa los chunks residentes con `HashMap<ChunkKey, u8, Capacity>` (`(cx,cy) -> ranura`) en vez de recorrer los slots; lo ejercita la demo `111_xlimited_sidescroller`.

## 2. Inventario

| Cabecera | Tipos / funciones | Equivalente STL |
|---|---|---|
| `type_traits.hpp` | `is_same`, `remove_cvref`, `conditional`, `enable_if`, `is_integral`/`is_arithmetic`/`is_pointer`/`is_enum`/`is_class`/`is_trivially_copyable`, `make_unsigned`, `underlying_type`, `to_underlying` | `<type_traits>` |
| `util.hpp` | `move`, `forward`, `swap`, `exchange`, `as_const`, `min`/`max`/`clamp` genéricos | `<utility>` |
| `bit.hpp` | `popcount`, `countl_zero`/`countr_zero`/`countl_one`/`countr_one`, `bit_width`, `has_single_bit`, `bit_floor`, `bit_ceil`, `rotl`/`rotr`, `bswap16`/`bswap32`, `bit_cast` | `<bit>` |
| `algorithm.hpp` | `find(_if)`, `contains`, `count(_if)`, `all_of`/`any_of`/`none_of`, `for_each`, `transform`, `copy`/`copy_n`/`fill_n`, `equal`, `accumulate`, `min_element`/`max_element`, `lower_bound`/`upper_bound`/`binary_search`, `reverse`, `rotate`, `iota`, `remove_if`, `unique` | `<algorithm>`, `<numeric>` |
| `array.hpp` | `Array<T, N>` | `std::array` |
| `bitset.hpp` | `BitSet<N>` | `std::bitset` |
| `allocator.hpp` | `Allocator` (concepto), `NullAlloc`, `BumpAlloc`, `InlineAlloc<N>` | (sin equivalente) |
| `arena_alloc.hpp` | `ArenaAlloc` (sobre `eng::LinearArena`) | (sin equivalente) |
| `hash.hpp` | `hash_u8/u16/u32`, `hash_value`, `hash_bytes`/`hash_string`, `Hash<T>` | `std::hash` |
| `static_vector.hpp` | `StaticVector<T, N>` (capacidad fija) | (sin equivalente) |
| `small_vector.hpp` | `SmallVector<T, N, A>` (inline + arena) | `llvm::SmallVector` |
| `vector.hpp` | `Vector<T, A>` (crece en arena) | `std::vector` (sin heap) |
| `chunked_vector.hpp` | `ChunkedVector<T, Chunk, Max, A>` (direcciones estables) | (sin equivalente) |
| `ring_buffer.hpp` | `RingBuffer<T, N>` (doble) | (sin equivalente) |
| `stack_queue.hpp` | `Stack<T,N>`, `Queue<T,N>`, `Deque<T,N>` | `std::stack`/`queue`/`deque` (fijos) |
| `enum_set.hpp` | `EnumSet<E, N>` | (sin equivalente) |
| `pool.hpp` | `Pool<T, N>` (+ `Handle` generacional) | `boost::pool` / slot map |
| `priority_queue.hpp` | `PriorityQueue<T, N, Cmp>` (+ `Less`/`Greater`) | `boost::heap` |
| `intrusive_list.hpp` | `IntrusiveList<T>`, `IntrusiveSList<T>` (+ `IntrusiveLink`/`IntrusiveSLink`) | `boost::intrusive::list` |
| `flat_map.hpp` | `FlatMap<K, V, N>` | `flat_map` (Boost) |
| `flat_set.hpp` | `FlatSet<T, N>` | (sin equivalente) |
| `hash_map.hpp` | `HashMap<K, V, N>` | `std::unordered_map` (fijo) |
| `hash_set.hpp` | `HashSet<T, N>` | `std::unordered_set` (fijo) |
| `dynamic_hash_map.hpp` | `DynamicHashMap<K, V, A>` (crece con rehash) | `std::unordered_map` (sin heap) |
| `direct_map.hpp` | `DirectMap<V, N>` (clave densa) | (sin equivalente) |
| `optional.hpp` | `Optional<T>` | `std::optional` |
| `expected.hpp` | `Expected<T, E>`, `unexpected(e)` | `std::expected` |
| `string_view.hpp` | `StringView` | `std::string_view` |
| `static_string.hpp` | `StaticString<N>` | (sin equivalente; `llvm::SmallString`) |
| `scope_guard.hpp` | `ScopeGuard`, `make_scope_guard` | `boost::scope_exit` |
| `stats.hpp` | `sum`/`mean`/`variance`/`stddev`/`kth_smallest`/`median`/`histogram`/`ema`/`RunningMean` | (sin equivalente; estadística) |
| `color.hpp` | `rgb444`/`lerp444`/`scale444`/`hsv_to_rgb444` | (sin equivalente; color Amiga) |
| `collision.hpp` | `Aabb`, `aabb_*`, `segments_intersect`, `point_in_triangle`, `circle_overlap` | (sin equivalente; juego 2D) |
| `text.hpp` | `trim`/`split_next`/`equal_ci`/`parse_u32`/`parse_s32`/`to_chars_*`/`join` | (parte de `boost::string`/`charconv`) |
| `grid.hpp` | `TileCoord`/`grid_to_world`/`world_to_grid`/`iso_to_screen`/`Hex` | (sin equivalente; rejilla/iso/hex) |
| `broadphase.hpp` | `SpatialHash<CellSize,CellsX,CellsY,MaxItems>` | (sin equivalente; broadphase) |
| `pathfinding.hpp` | `bfs<W,H>`, `astar<W,H>`, `reconstruct_path<W,H>` | (sin equivalente; A*/BFS) |
| `dsp.hpp` | `Adsr`, `OnePole`, `DelayLine`, `soft_clip`, `osc_*` | (sin equivalente; audio) |
| `function_ref.hpp` | `FunctionRef<Sig>` | `std::function_ref` (C++26) |

## 3. Reglas de diseño para Amiga 500

- **Sin heap**: nada usa `malloc`. Los contenedores de capacidad fija (`StaticVector`, `RingBuffer`, `Array`, `BitSet`, `FlatMap`/`FlatSet`, `HashMap`/`HashSet`, `DirectMap`) reservan inline; los que crecen (`Vector`, `SmallVector`, `ChunkedVector`) lo hacen sobre un `Allocator` (bump/arena).
- **Crecimiento explícito y de fase `init`**: crecer devuelve `false`/`nullptr` si no cabe (nunca aborta); en `frame` se reserva de antemano o se usan contenedores de capacidad fija.
- **Listas intrusivas**: el enlace vive en el objeto (`IntrusiveList`/`IntrusiveSList`), así que insertar/borrar es `O(1)` y **no asigna**; la lista no posee los nodos. Es la alternativa del engine a `std::list` (nodos con heap).
- **Rehash solo en `init`**: `DynamicHashMap` rehace sus tablas desde un `Allocator` al cruzar 3/4 de carga; el coste es `O(n)` en ese momento y la tabla vieja se descarta (bump). Para `frame` usar `HashMap`/`FlatMap` de capacidad fija.
- **Hash sin libcalls**: `hash.hpp` usa `mulu.w` (16×16) o mezcla de rotaciones/xors/sumas, nunca multiplicación de 32×32; la sonda de codegen no muestra `__mulsi3` ni instrucciones de 68020.
- **Almacenamiento crudo**: `Vector`/`SmallVector`/`ChunkedVector`/`DirectMap` exigen `T` copiable trivialmente (no hay `new` de colocación en freestanding); mapas y sets exigen claves/valores construibles por defecto.
- **Ancho exacto en la aritmética de bits**: `eng::u32` es `unsigned long`, que mide
  4 bytes en m68k pero 8 en algunos hosts. `bit.hpp` enmascara al ancho real de `T`
  y `BitSet` usa palabras de 32 bits exactos (`__UINT32_TYPE__`).
- **`constexpr` y `always_inline`**: las operaciones diminutas se resuelven en
  compilación o se inlinan (un `jsr`+`rts` cuesta más que el cálculo en 68000).
- **Fallos de rango = parada, no corrupción**: `at()` (y los accesos de
  `Optional`/`Expected`) disparan `illegal` (0x4afc) en m68k, reutilizando
  `eng::detail::span_out_of_bounds` (`span.hpp`), detectable por el emulador.
- **Coste visible**: las limitaciones se documentan en la cabecera, no se esconden.
  `Optional`/`Expected` mantienen presente el almacenamiento de sus tipos (no hay
  `new` de colocación en freestanding) y exigen `T` construible por defecto.

### 3.b Soporte por escalar

Los contenedores y las utilidades de bytes/enteros son agnósticos del tipo (almacenan `T` y usan solo `operator<`/`==`/`+`); las utilidades **escalar-genéricas** (`stats`, `dsp`) usan `mul_norm`/`div_norm` y los rasgos del escalar. No hacen falta especializaciones: `MiniFloat16` y `Fixed` funcionan igual.

| Utilidad | enteros | `Fixed` (p. ej. q12) | `MiniFloat16` | `float`/`double` |
|---|---|---|---|---|
| Contenedores (`Array`…`RingBuffer`, `FlatMap`, `HashSet`, `Pool`, `PriorityQueue`…) | sí | sí | sí | sí |
| `algorithm.hpp`, `core/sort.hpp`, `type_traits`/`util`/`bit` | sí | sí | sí | sí |
| `collision`, `color`, `grid`, `broadphase`, `pathfinding`, `text` | sí | — | — | — (enteros por diseño: 68000/hardware) |
| `stats` | — | sí (sum/mean con acumulador s32; stddev con `fixed_math.hpp`) | sí (~1e-3) | sí |
| `dsp` | — | sí (osc_sine con `fixed_math.hpp`) | sí | sí |

Limitaciones (también en cada cabecera):

- `stats::sum`/`mean` con `Fixed<s16>` acumulan en **32 bits** (`add.l`) y solo estrechan al final; `variance` mantiene el acumulador del escalar (un acumulador ancho necesitaría productos de 64 bits, `__muldi3`).
- `stats::stddev` con `Fixed` requiere incluir `eng/core/fixed_math.hpp` (aporta `scalar_sqrt<Fixed>` vía `isqrt`); `dsp::osc_sine` con `Fixed` también (aporta `scalar_sin<Fixed>`).
- **Matemáticas `Fixed`**: `eng/core/fixed_math.hpp` especializa `scalar_sin`/`scalar_cos`/`scalar_tan`/`scalar_asin`/`scalar_acos`/`scalar_atan2`/`scalar_sqrt`/`scalar_exp2`/`scalar_log2`/`scalar_exp`/`scalar_log`/`scalar_pow` para `Fixed<s16,E>` (tablas compartidas + `isqrt`) y añade `wrap_angle`/`angle_diff` (pliegue sin tabla); incluir ese header antes de usar easings `_sine`/`_expo`, `smooth_damp`, `length`/`normalize`/`project`/`reflect` o `stddev` con fixed. `sin`/`cos` requieren `E <= 14`; `atan2`/`asin`/`acos` `E <= 13` (π debe caber); `tan` satura donde `cos ≈ 0`; `exp2` satura; `log2` solo correcto dentro del rango del fixed. Tablas (seno 2 KiB, `exp2`/`log2`/`atan` 1 KiB c/u) compartidas, con **tamaño elegible por plantilla** (`fixed_sin<E,Size,Iter>`, `fixed_exp2`/`fixed_log2`/`fixed_atan2<E,Size>`) o **por compilación** (`-DENG_FIXED_SIN_SIZE`, `ENG_FIXED_EXP2_SIZE`, `ENG_FIXED_LOG2_SIZE`, `ENG_FIXED_ATAN_SIZE`, `ENG_FIXED_SIN_ITER`; medido: ~5 KiB por defecto, ~1.8 KiB con 512/64/64/64). Verificado por HOST-104 y **por demo** (`110_ylimited_shooter`, self-test en `init`).
- `MiniFloat16`: pierde incrementos por debajo de `2^-14` (tasas de ADSR/`alpha` muy pequeñas bajoflow a 0); precisión ~1e-3.
- `Fixed`: la división (`div_norm`) **satura**; el paso mínimo es `2^-Exp` (p. ej. 1/4096 en q12).
- Las matemáticas de escalares (interpolación, easings, geometría, ruido, `minifloat_math`) viven en `eng::math`; ver `SCALAR_LIBRARY.md` (tabla función × escalar) y `MATH_LIBRARY.md`.

## 4. Qué no incluye (y por qué)

- **Contenedores con heap implícito** (`std::string`, nodos de `std::map`/`std::list`): fuera. Sí hay `Vector`/`SmallVector`/`ChunkedVector` y mapas/sets, pero **sin `malloc`**: crecen sobre un `Allocator` (arena) y solo en `init`/carga.
- **`std::function`**: usa heap y copia el cierre; en su lugar, `FunctionRef` cuando solo hace falta pasar un callable sin poseerlo.
- **`variant`/`tuple`/`mdspan`** y el resto de la STL: sin consumidor real en el engine, no se portan (el tamaño de código y el tiempo de compilación son recursos).
- **`sort`**: ya existe en `eng/core/sort.hpp`.
- **`map` de árbol (red-black)**: para esta escala pierde frente a `FlatMap` (pequeño) y `HashMap` (grande), y añade mucho código.
- **Flags y restauraciones hechos a mano**: no inventar máscaras ni bloques de limpieza por cada `return`; usa `EnumSet<E>` para conjuntos de flags tipados y `ScopeGuard` para restaurar estado (DMA, registros, color) en cualquier salida. Ver §7.

## 5. Verificación

Cada cabecera tiene su test unitario **host** (`tests/host/`), que es la forma
canónica de validar algoritmos puros (sin hardware):

| Test | Cubre |
|---|---|
| HOST-073 | `type_traits.hpp`, `util.hpp` |
| HOST-074 | `bit.hpp` |
| HOST-075 | `algorithm.hpp` |
| HOST-076 | `array.hpp`, `bitset.hpp` |
| HOST-077 | `static_vector.hpp`, `ring_buffer.hpp` |
| HOST-078 | `optional.hpp`, `expected.hpp` |
| HOST-079 | `string_view.hpp`, `function_ref.hpp` |
| HOST-080 | `allocator.hpp`, `hash.hpp` |
| HOST-081 | `vector.hpp`, `small_vector.hpp` |
| HOST-082 | `flat_map.hpp`, `flat_set.hpp` |
| HOST-083 | `hash_map.hpp`, `hash_set.hpp` (back-shift, estrés contra referencia) |
| HOST-084 | `arena_alloc.hpp`, `chunked_vector.hpp` |
| HOST-085 | `direct_map.hpp` |
| HOST-086 | `dynamic_hash_map.hpp` (rehash, estrés contra referencia e internado) |
| HOST-087 | `intrusive_list.hpp` (`IntrusiveList`/`IntrusiveSList`, free-list) |
| HOST-088 | `pool.hpp` (handles generacionales, reciclado de slots) |
| HOST-089 | `priority_queue.hpp` (max/min-heap, comparador propio) |
| HOST-090 | `core/sort.hpp` (stable/nth/partial/radix) |
| HOST-091 | `stack_queue.hpp`, `enum_set.hpp` (y `RingBuffer` doble) |
| HOST-092 | `scope_guard.hpp`, `static_string.hpp` |
| HOST-093 | `stats.hpp` (media/varianza/orden/histograma, `double`/MF/`q12`) |
| HOST-094 | `color.hpp` (RGB444/lerp/scale/HSV) |
| HOST-095 | `collision.hpp` (AABB/segmento/triángulo/círculo) |
| HOST-096 | `text.hpp` (trim/split/parse/to_chars/join) |
| HOST-097 | `grid.hpp` (tile/iso/hex) |
| HOST-098 | `broadphase.hpp` (SpatialHash) |
| HOST-099 | `pathfinding.hpp` (BFS/A*/reconstruct) |
| HOST-100 | `core/random.hpp` (next_range/pick/shuffle/gaussian) |
| HOST-101 | `core/noise.hpp` (worley/turbulence/ridged) |
| HOST-102 | `dsp.hpp` (Adsr/OnePole/DelayLine/osciladores; `double`/MF/`q12`) |
| HOST-103 | util (contenedores/algoritmos) con `MiniFloat16`/`q12` |
| HOST-104 | `core/fixed_math.hpp` (sin/cos/tan/atan2/asin/acos/sqrt/exp2/log2 de `Fixed`; easings/length con q12) |

> **Estado: verificación por demo parcial.** `BitSet` y `StaticVector` están **verificadas** por la demo `086_bob_objects` (`build -> run -> analyze` OK), que las ejerce a través de `eng/scene/actor.hpp` (`ActorStore` y `emit_bob_fallbacks`); además las respaldan HOST-076 (`BitSet`) y HOST-077 (`StaticVector`). `RingBuffer` está **verificada** por la demo `081_background_tasks` (media móvil del throughput del fondo), `FlatMap` por la demo `078_math3d_solid` (`eng::assets::Blob` indexa sus chunks por tipo), `DirectMap` por la demo `066_polyphony` (`eng::audio::SampleBank` indexa los sonidos por id), `IntrusiveSList` por `081_background_tasks` (free-list de `BackgroundQueue`), `Pool` por `086_bob_objects` (parque de actores), `HashMap` por `111_xlimited_sidescroller` (índice de chunks de `ChunkCache`), `color` también por `086_bob_objects` (gradiente del cielo con `eng::util::lerp444`), y `broadphase` y `pathfinding` por `110_ylimited_shooter` (self-test en `init`: `SpatialHash` + `bfs`/`reconstruct_path` en el 68000; si falla, la demo no llega a READY). Los demás contenedores (`Vector`, `SmallVector`, `ChunkedVector`, `IntrusiveList`, `FlatSet`, `HashSet`, `DynamicHashMap`, `PriorityQueue`, `Stack`/`Queue`/`Deque`, `EnumSet`, `ScopeGuard`, `StaticString`, `stats`, `collision`, `text`, `grid`, `dsp`, `allocator`/`arena_alloc`/`hash`) están respaldados por HOST-080..102 y siguen **NO VERIFICADOS por demo**; pueden cambiar sin aviso (`docs/testing/README.md`).

Los tests se ejecutan con el `g++` del entorno (Windows/MinGW, donde `unsigned long`
mide 4 bytes y coincide con m68k) mediante `tools/run-host-tests.sh`.

## 6. Cómo añadir una utilidad

1. Comprobar que no existe ya en `eng/core/` ni en `eng/core/util/` (§1.6 de
   `AGENTS.md`).
2. Cabecera en `engine/include/eng/core/util/`, en `eng::util`, con comentario
   didáctico: intención, coste, límites por plataforma y ejemplo de uso.
3. Test host en `tests/host/` con su `README.md`.
4. Si la utilidad es de bits o entra en un bucle caliente, añadir una sonda al gate
   de codegen (`tools/analyze/codegen-report.mjs`) para fijar que no aparecen libcalls
   de libgcc ni instrucciones de 68020. Ya está cubierto el vocabulario sensible:
   `hash.hpp` (`c_hash_u16`/`c_hash_u32`), `hash_map.hpp`/`hash_set.hpp`
   (`c_hashmap_find`/`c_hashset_contains`), `vector.hpp`/`chunked_vector.hpp`
   (`c_vector_grow`/`c_chunked_push`), `pool.hpp`/`priority_queue.hpp`/`intrusive_list.hpp`
   (`c_pool_ops`/`c_pq_ops`/`c_ilist_ops`), la ordenación de `core/sort.hpp`
   (`c_stable_sort`/`c_nth_element`/`c_radix_u16`), `dynamic_hash_map.hpp` (`c_dyn_hashmap`),
   `stats.hpp` (`c_stats_ops`) y `color.hpp`/`collision.hpp`/`text.hpp`
   (`c_color_lerp`/`c_collision_ops`/`c_text_ops`), `grid.hpp` (`c_grid_ops`),
   `broadphase.hpp` (`c_broadphase_ops`), `pathfinding.hpp` (`c_pathfinding_ops`),
   `core/random.hpp` (`c_random_ops`) y `dsp.hpp` (`c_dsp_ops`).
5. Antes de añadir una utilidad nueva, comprobar si el **vocabulario** de §7 ya cubre la
   necesidad (p. ej. flags con `EnumSet`, restauración con `ScopeGuard`, colas con
   `Queue`/`Deque`); adoptarlo en el engine y documentarlo aquí.

## 7. Guía de elección

Qué usar según la necesidad, con el criterio del A500 (sin heap; coste visible):

| Necesidad | Usar |
|---|---|
| Secuencia contigua de tamaño fijo | `Array<T, N>` |
| Vector que crece (fase `init`) | `Vector<T, A>` o `SmallVector<T, N, A>` |
| Secuencia que casi siempre cabe en pocos | `SmallVector<T, N, A>` |
| Direcciones estables al crecer | `ChunkedVector<T, Chunk, Max, A>` |
| Pila / cola / doble cola | `Stack<T,N>` / `Queue<T,N>` / `Deque<T,N>` |
| Cola circular de capacidad fija | `RingBuffer<T, N>` |
| Mapa/conjunto pequeño y ordenado | `FlatMap<K,V,N>` / `FlatSet<T,N>` |
| Clave densa `0..N-1` | `DirectMap<V, N>` |
| Flags de estado por `enum` | `EnumSet<E, N>` |
| Mapa/conjunto hash grande (fijo) | `HashMap<K,V,N>` / `HashSet<T,N>` |
| Mapa hash que crece (fase `init`) | `DynamicHashMap<K,V,A>` |
| Parque de objetos con handle estable | `Pool<T, N>` |
| Lista de objetos sin asignar | `IntrusiveList<T>` / `IntrusiveSList<T>` |
| Prioridad / heap | `PriorityQueue<T, N, Cmp>` |
| Ordenar en su sitio | `quick_sort` (`core/sort.hpp`) |
| Ordenar estable / top-k / por conteo | `stable_sort`, `nth_element`, `partial_sort`, `radix_sort_u16` |
| Valor opcional / resultado con error | `Optional<T>` / `Expected<T, E>` |
| Vista de texto / construir texto sin heap | `StringView` / `StaticString<N>` |
| Restaurar estado al salir del ámbito | `ScopeGuard` |
| Estadística / telemetría (fps, carga) | `stats.hpp` (`mean`/`variance`/`ema`/`RunningMean`) |
| Color RGB444 (paleta/fundido) | `color.hpp` |
| Colisión 2D (AABB/segmento/círculo) | `collision.hpp` |
| Texto (config/HUD: parseo/emisión) | `text.hpp` |
| Coordenadas tile/iso/hex | `grid.hpp` |
| Broadphase (rejilla espacial) | `broadphase.hpp` |
| Búsqueda de caminos (A*/BFS) | `pathfinding.hpp` |
| Aleatoriedad (rango/pick/shuffle) | `core/random.hpp` (`Xoroshiro64pp` + distribuciones) |
| Ruido procedural (value/fbm/worley) | `core/noise.hpp` |
| Audio/efectos (envolvente/filtro/eco/oscilador) | `dsp.hpp` |
| Pasar un callable sin poseerlo | `FunctionRef<Sig>` |

Notas de uso:

- **`EnumSet`** sustituye a las máscaras manuales: `set(Flag::X)` en vez de `1u << k`, y el compilador rechaza valores de otro enum. Sus valores deben caer en `[0, N)`.
- **`ScopeGuard`** es la forma de garantizar una restauración (DMA, `COLOR00`, un banco de arena) aunque el camino salga antes; `release()`/`commit()` cuando termina bien.
- **`Pool`** es el patrón por defecto para "muchos objetos con handle" (`ActorStore` ya lo usa); **`HashMap`/`FlatMap`** se reservan para búsquedas por clave.
- **Estructuras de capacidad fija** en `frame`; reservar/crecer (`Vector`, `SmallVector`, `DynamicHashMap`) solo en `init`/carga.

Plan de crecimiento (qué falta y en qué orden, con sus gates): [ROADMAP_UTIL_LIBRARY.md](../../guides/roadmap/ROADMAP_UTIL_LIBRARY.md).
