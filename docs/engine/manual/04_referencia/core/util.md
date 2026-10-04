# Referencia — `core/util/`

Utilidades y contenedores **sin dependencias del SO**: lo que en libstdc++ tiraría de heap, excepciones o RTTI, aquí es `constexpr`/`noexcept` y de capacidad **paramétrica de plantilla**. El engine no usa heap tras el arranque; cuando necesita un contenedor dinámico, usa `chunked_vector`/`allocator` con un pool preasignado.

## Resultados y valores opcionales

| Tipo | Uso |
|---|---|
| `Expected<T, E>` / `Expected<void, E>` (`expected.hpp:58`, `:125`) y `Unexpected<E>` (`expected.hpp:47`) | Resultado que lleva **error explícito**, como alternativa a excepciones (deshabilitadas). |
| `Optional<T>` (`optional.hpp:37`) | Valor presente/ausente, sin `std::optional`. |
| `Variant<Ts...>` (`variant.hpp:68`) | Unión discriminada con visitante (`NthType`/`IndexOf`, `variant.hpp:31`). |

## Contenedores de capacidad fija

`StaticVector<T, N>` (`static_vector.hpp:35`), `RingBuffer<T, N>` (`ring_buffer.hpp:27`), `Pool<T, N>` (`pool.hpp:28`) — reservan su almacenamiento **en línea** (en el propio objeto, no en heap) y su tamaño es parámetro de plantilla. `small_vector`, `chunked_vector`, `vector`, `array`, `stack_queue`, `priority_queue`, `interval`, `flat_map`/`flat_set`, `hash_map`/`hash_set`, `sparse_set`, `direct_map`, `intrusive_list`, `lru_cache` (`lru_cache.hpp:27`) y `grid` completan la caja de herramientas, con la misma política de capacidad explícita.

## Texto

`StringView` (`string_view.hpp:27`), `static_string` (buffer de tamaño fijo), `dynamic_string` (con almacén propio) y `string_interner` (interning de símbolos). No usan heap salvo donde se declara explícitamente.

## Memoria

`allocator`, `heap_alloc`, `arena_alloc` (interfaz de asignación con política; ver `core/memory/` y `docs/engine/architecture/MEMORY_OWNERSHIP.md`).

## Control de flujo y RAII

`ScopeGuard<F>` (`scope_guard.hpp:26`) ejecuta una acción al salir del ámbito (limpieza sin excepciones). `FunctionRef<Sig>` (`function_ref.hpp:30`) es una referencia a callable **no propietaria** (sin `std::function`, sin reserva). `StateMachine<State, Event>` con `Transition` (`state_machine.hpp:45`, `:38`) para lógica de estados. `Event`, `task`, `type_traits`, `util` y `algorithm` cierran el módulo.

## Bits y binario

`bitset`, `dynamic_bitset`, `bit`, `bitstream` — manipulación de bits con y sin tamaño fijo.

## Dominios de algoritmo

`collision` / `broadphase`, `pathfinding` / `graph` / `union_find`, `dsp`, `color`, `text`, `stats`, `quantizer`, `intmath` (aritmética entera de apoyo). Son utilidades genéricas reutilizables por los módulos de `ai/`, `sim/`, `board/` y `cards/`.

Volver al [índice de `core/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
