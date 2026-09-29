# HOST-333 — idioma de error (`eng::Expected<T>`)

Respalda `engine/include/eng/core/types/result.hpp`: el análogo de `std::expected<T, E>` del
engine (sin excepciones ni heap), el **idioma único de error** de las APIs nuevas. Cubre:

- construcción desde **valor** (`ok()`, `status() == Ok`, `value()`/`operator*`);
- construcción desde **error** (`!ok()`, `status()`), con `value_or(fallback)`;
- uso típico: una función devuelve **valor o `eng::Result`** sin out-params ni `0`/bloque
  inválido.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/333_expected
```

Ver `docs/engine/architecture/ROADMAP_API_COHERENCE.md` (F1).
