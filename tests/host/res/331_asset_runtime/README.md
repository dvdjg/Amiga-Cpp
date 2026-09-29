# HOST-331 — runtime de assets (`eng::res::AssetRuntime`)

Respalda `engine/include/eng/res/asset_runtime.hpp`: la pieza que engancha la **caché**
(`AssetCache`) con el **enrutado de E/S** (`route_io`) para cargar de disco de forma
asíncrona desde el bucle. Con un backend de ficheros falso valida:

- `load(path, size, bank)` → declara y **lanza** la carga (estado `Loading`);
- `on_msg(FileDone)` → completa la carga (estado `Ready`), con datos y presupuesto;
- `get(id)` da los bytes;
- un mensaje que no es de E/S (`VBlank`) **no** se consume.

El runtime **posee** su `CacheBackend` (la caché guarda una `Ref`, no el valor); por eso el
`m_backend` interno debe sobrevivir a la caché.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/331_asset_runtime
```

Enganchado en hardware por `AmigaBackend::assets()` (`eng/platform/amiga/backend.hpp`), que
inicializa el runtime en `configure_memory` con el presupuesto de las arenas Chip/Slow.
