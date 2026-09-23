# HOST-233: fachada pública del engine (`eng/api/api.hpp`)

Test host de la **fachada pública**: comprueba que un solo `#include <eng/api/api.hpp>` expone
las cabeceras estables de la API sin necesidad de incluir cada una por separado.

## Qué comprueba

1. `eng::Box` (rectángulo de UI) y sus helpers.
2. `eng::graphics::FramePlan` y los valores preparados de Blitter `OrBob`/`LineEor`/`C2p4`.
3. `eng::field::kCpuRaster` (rasterizador CPU por defecto).
4. `eng::input::InputAggregator` y `eng::task::BackgroundQueue`.
5. `eng::Palette32`.
6. El contrato `eng::GameModule` es visible (con un backend ficticio, en `static_assert`).

No instancia un backend (el backend va en `main()` y se pasa a `eng::Engine`): la fachada es
deliberadamente agnóstica y no incluye `eng/platform/amiga_minimal.hpp`.

## Salida de referencia

```
OK: fachada publica (eng/api/api.hpp) expone la API estable.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/core/233_api_facade
```
