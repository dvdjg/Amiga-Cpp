# HOST-208 - ptr

Test host de `eng/core/ptr.hpp`: punteros "inteligentes" **sin heap** para el engine.

- `Ref<T>`: observador **no propietario** y anulable (sustituye al `T*` crudo en APIs).
- `NonNull<T>`: como `Ref` pero con contrato "no nulo".
- `Opt<T>`: **opcional en sitio** (sin `std::optional`/heap).

## Build / run

```
CXX=<g++> bash tools/run-host-tests.sh tests/host/208_ptr
```

Valida construcción desde referencia (implícita) y desde `T*` (explícita), `valid()`/`operator bool`,
`operator*`/`->`, comparación, `reset()`, y el ciclo vacío/valor de `Opt`.
