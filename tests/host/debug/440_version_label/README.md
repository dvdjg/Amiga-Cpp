# HOST-440 - `version_label` (versionado de demos)

Test host de la norma de **versionado** (AGENTS §0, `PROCEDIMIENTO_DEMOS_Y_JUEGOS.md` §3.18):
toda demo lleva `major.minor.build` y lo muestra en la **franja inferior de 16 px y 4 bpp**
(nombre + versión + depuración). Valida la pieza pura `eng/debug/version.hpp`:

- formato `vM.m.B` (conversión entera sin stdlib),
- build por defecto desde `-DENG_BUILD_NUMBER` (lo inyecta `tools/build/build-demo.sh` con el
  contador persistente `out/.build-number`),
- valores grandes (65535/4294967295),
- capacidad corta: trunca sin desbordar y siempre NUL-termina.

Es freestanding (sin hardware, sin heap, sin STL).

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/debug/440_version_label   # solo este
bash tools/run-host-tests.sh                                      # todos
```
