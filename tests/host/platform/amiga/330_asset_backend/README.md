# HOST-330 — backend Amiga de `AssetCache` (`AssetCacheBackend`)

Respalda `engine/include/eng/platform/amiga/asset_backend.hpp`: el pegamento con el que la
caché de assets (`eng/res/asset_cache.hpp`) funciona en hardware (memoria de las arenas +
E/S del mini-SO). Como la E/S la aporta `eng::os`, el test implementa un backend de ficheros
**falso** y valida:

- `alloc` en **Chip** y en **Slow** (para `MemBank::Fast`), y que `free` es **no-op** (arenas
  *bump*);
- `load`: abre y lanza `file_read_async` con el cookie `IoUser{'A', id}` correcto;
- integración con `res::AssetCache`: `declare` → `get` (lanza la carga, estado `Loading`) →
  `on_load_done` (estado `Ready`, datos y presupuesto).

La finalización real llega como `FileDone`/`FileError` y se enruta con `res::route_io`
(HOST-255, pendiente de engarzar en el bucle del `App`).

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/platform/330_asset_backend
```
