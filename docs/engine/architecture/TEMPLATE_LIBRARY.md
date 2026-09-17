# Librería de plantillas de utilidades (`eng::util`)

`engine/include/eng/core/util/` reúne las utilidades genéricas del engine que en un
programa hosted vendrían de la STL y que el runtime Amiga **no puede usar**: el cruce
es freestanding (`-nostdlib`, sin `libstdc++`, sin excepciones, sin RTTI, sin heap
durante el gameplay). No es una reimplementación completa de la librería estándar:
es una selección mínima de piezas que se usan de verdad y que se han elegido por su
relación valor/coste en un 68000.

Todas las cabeceras son **header-only**, `constexpr`, sin asignación dinámica y sin
virtuals. El vocabulario vive en el espacio de nombres `eng::util`; las matemáticas
de escalares (`Fixed`, `MiniFloat16`, `lerp`…) siguen en `eng::math`.

## 1. Encaje con lo que ya existe

La librería **complementa** el núcleo de `eng/core/`, no lo duplica:

```
   eng/core/                         eng/core/util/  (esta librería)
   ─────────────                     ─────────────────────────────
   types.hpp    tipos base           type_traits.hpp  rasgos
   span.hpp     Span<T>      ◄────── algorithm.hpp    algoritmos sobre Span
   sort.hpp     quick_sort           util.hpp         move/forward/swap/exchange
   fast_div.hpp is_pow2/ilog2 ◄───── bit.hpp          popcount/clz/rotl/bswap
   ct_array.hpp tabla por functor    array.hpp        Array<T,N> (agregado)
   typed.hpp    vistas de dominio    bitset.hpp       BitSet<N>
                                     static_vector.hpp StaticVector<T,N>
                                     ring_buffer.hpp  RingBuffer<T,N>
                                     optional.hpp     Optional<T>
                                     expected.hpp     Expected<T,E>
                                     string_view.hpp  StringView
                                     function_ref.hpp FunctionRef<Sig>
```

Puntos de reutilización explícitos:

- `has_single_bit` se apoya en `eng::is_pow2` (`fast_div.hpp`); no se redefine la
  detección de potencia de dos.
- El orden de elementos lo cubre `eng/core/sort.hpp` (`quick_sort`/`sort_items`);
  `algorithm.hpp` no incluye una segunda ordenación.
- `Array<T,N>` y `ct_array<T,N>` son distintos a propósito: el primero es un agregado
  que se inicializa con llaves como un array C; el segundo genera su contenido en
  compilación desde un functor.
- Los contenedores de capacidad fija siguen el patrón de handles/pool de `eng/task/background.hpp` (sin heap, con `valid()` explícito donde aplica).
- `eng/scene/actor.hpp` es el primer consumidor dentro del engine: `ActorStore` usa `BitSet<MaxActors>` para los slots vivos del parque generacional y `emit_bob_fallbacks` usa `StaticVector<u16, MaxActors>` para los degradados a BOB.

## 2. Inventario

| Cabecera | Tipos / funciones | Equivalente STL |
|---|---|---|
| `type_traits.hpp` | `is_same`, `remove_cvref`, `conditional`, `enable_if`, `is_integral`/`is_arithmetic`/`is_pointer`/`is_enum`/`is_class`/`is_trivially_copyable`, `make_unsigned`, `underlying_type`, `to_underlying` | `<type_traits>` |
| `util.hpp` | `move`, `forward`, `swap`, `exchange`, `as_const`, `min`/`max`/`clamp` genéricos | `<utility>` |
| `bit.hpp` | `popcount`, `countl_zero`/`countr_zero`/`countl_one`/`countr_one`, `bit_width`, `has_single_bit`, `bit_floor`, `bit_ceil`, `rotl`/`rotr`, `bswap16`/`bswap32`, `bit_cast` | `<bit>` |
| `algorithm.hpp` | `find(_if)`, `contains`, `count(_if)`, `all_of`/`any_of`/`none_of`, `for_each`, `transform`, `copy`/`copy_n`/`fill_n`, `equal`, `accumulate`, `min_element`/`max_element`, `lower_bound`/`upper_bound`/`binary_search`, `reverse`, `rotate`, `iota`, `remove_if`, `unique` | `<algorithm>`, `<numeric>` |
| `array.hpp` | `Array<T, N>` | `std::array` |
| `bitset.hpp` | `BitSet<N>` | `std::bitset` |
| `static_vector.hpp` | `StaticVector<T, N>` | (sin equivalente: capacidad fija) |
| `ring_buffer.hpp` | `RingBuffer<T, N>` | (sin equivalente) |
| `optional.hpp` | `Optional<T>` | `std::optional` |
| `expected.hpp` | `Expected<T, E>`, `unexpected(e)` | `std::expected` |
| `string_view.hpp` | `StringView` | `std::string_view` |
| `function_ref.hpp` | `FunctionRef<Sig>` | `std::function_ref` (C++26) |

## 3. Reglas de diseño para Amiga 500

- **Sin heap**: ningún contenedor asigna. `StaticVector`/`RingBuffer` reservan su
  capacidad inline; `Array`/`BitSet` son de tamaño fijo.
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

## 4. Qué no incluye (y por qué)

- **Contenedores dinámicos** (`vector`/`map`/`string`): prohibido asignar en gameplay;
  el engine usa arenas, pools y handles.
- **`std::function`**: usa heap y copia el cierre; en su lugar, `FunctionRef` cuando
  solo hace falta pasar un callable sin poseerlo.
- **`variant`/`tuple`/`mdspan`** y el resto de la STL: sin consumidor real en el
  engine, no se portan (el tamaño de código y el tiempo de compilación son recursos).
- **`sort`**: ya existe en `eng/core/sort.hpp`.

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

> **Estado: verificación por demo parcial.** `BitSet` y `StaticVector` están **verificadas** por la demo `086_bob_objects` (`build -> run -> analyze` OK), que las ejerce a través de `eng/scene/actor.hpp` (`ActorStore` y `emit_bob_fallbacks`); además las respaldan HOST-076 (`BitSet`) y HOST-077 (`StaticVector`). El resto de utilidades siguen **NO VERIFICADAS (solo test host)** y pueden cambiar sin aviso (`docs/testing/README.md`).

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
   de libgcc ni instrucciones de 68020.
