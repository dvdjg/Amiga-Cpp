# HOST-332 — vista tipada de assets (`AssetRuntime::bytes<Tag>`)

Respalda la "decodificación de frontera" de `engine/include/eng/res/asset_runtime.hpp`: un
asset cargado (bytes) se expone como vista de **dominio** (`eng::ByteView<Tag>`) sin copiar.
Comprueba:

- antes de `Ready`, la vista está **vacía**;
- tras `on_msg(FileDone)`, la vista tiene el tamaño cargado, datos y alineación del dominio
  (`MusicTag` → 4);
- el flujo `load(path, size, bank)` → `bytes<Tag>(id)` es el que usará `app.load<Tag>`.

Los bytes se cargan tal cual (la vista es reinterpretación al dominio); los decoders de
formatos concretos (música/imágenes cocinadas) se añadirán encima.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/332_asset_typed
```
