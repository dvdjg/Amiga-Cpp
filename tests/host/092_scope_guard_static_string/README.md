# HOST-092 — ScopeGuard y StaticString

Respalda `engine/include/eng/core/util/scope_guard.hpp` y
`engine/include/eng/core/util/static_string.hpp`.

## Qué cubre

- **`ScopeGuard`** (RAII sin excepciones): la acción se ejecuta al salir del ámbito;
  `release()`/`commit()` la desactivan; el *move* transfiere la acción sin ejecutarla
  dos veces.
- **`StaticString<N>`** (buffer de texto de capacidad fija, siempre terminado en
  `'\0'`): `assign` (recorta a capacidad), `append` (char y `StringView`, rechaza sin
  escribir a medias si no cabe), `c_str`/`data`/`operator[]`/`view`, `clear`,
  `capacity = N-1`.

## Uso previsto

`ScopeGuard`: restaurar registros/DMA/estado en cualquier salida. `StaticString`: HUD,
marcadores y nombres generados sin heap, pareja de escritura de `StringView`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/092_scope_guard_static_string
```
