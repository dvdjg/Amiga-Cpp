# HOST-328 — carga tipada desde fichero (`eng::res::load_file<Tag>`)

Respalda la variante de **fichero** de `engine/include/eng/res/load.hpp`. Como la E/S
(`os::file_*`) la aporta el backend, el test **implementa un backend falso** sobre un
«sistema de ficheros» en memoria y valida la composición `abrir → medir → reservar → leer →
cerrar`:

- fichero existente: bloque válido, medio (Chip), alineación y datos copiados, con
  `out_bytes` = tamaño real del fichero;
- fichero inexistente → bloque inválido y `out_bytes = 0`;
- **lectura corta** → inválido (no se acepta parcial);
- **no cabe** en la arena → inválido (sin excepciones).

No toca hardware; el camino de compilación m68k se cubre con la demo que usa `res::load`
(213). La variante **asíncrona** de fichero (con `AssetCache` + `route_io`) es la siguiente
pieza.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/res/328_load_file
```
