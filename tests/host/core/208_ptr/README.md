# HOST-208 - ptr

Test host de `eng/core/ptr.hpp` y `eng/core/span.hpp`: punteros y vistas "inteligentes"
**sin heap** para el engine.

- `Ref<T>`: observador **no propietario** y anulable (sustituye al `T*` crudo en APIs).
- `NonNull<T>`: como `Ref` pero con contrato "no nulo".
- `Opt<T>`: **opcional en sitio** (sin `std::optional`/heap).
- `Span<T>`: vista contigua; se cubre aquí la **construcción** que evita ruido en las llamadas.

## Build / run

```
CXX=<g++> bash tools/run-host-tests.sh tests/host/208_ptr
```

`Ref`: construcción desde referencia, `T*` y `nullptr` (**implícitas**, para escribir
`f(t, table, nullptr)` sin nombrar el tipo), `valid()`/`operator bool`, `operator*`/`->`,
comparación, `reset()`.

`Span`: **CTAD** `{ptr, n}` sin nombrar el tipo (`detail::same` verifica que deduce `Span<int>`),
array sin count (deduce el tamaño) y **conversión cualificante** `Span<T> -> Span<const T>`
(como `std::span`).
