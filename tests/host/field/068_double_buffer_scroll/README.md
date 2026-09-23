# HOST-068: `DoubleBufferScrollPlayfield`

Test host de `engine/include/eng/field/double_buffer_playfield.hpp`: la superficie de scroll
con **doble buffer de bitmap** (dos bitmaps del mundo; el display lee el delantero y la app
escribe el trasero; `flip()` conmuta y el compositor instala la copperlist del nuevo
delantero).

Es el respaldo determinista que le faltaba a la demo 122 (F0.4 de
`docs/guides/roadmap/NORMALIZACION_REPO.md`).

## Qué comprueba

1. `begin()` reserva **dos bitmaps distintos** y expone la geometría (`row_bytes`, planos).
2. `front_index()` arranca en 0 y `flip()` alterna `0↔1`.
3. `hardware_view()` describe **siempre el buffer delantero**: base de bitplanes, paso
   (`total_bytes` en layout interleaved), `display_height` y viewport.
4. La cámara (`cam_x()`/`cam_y()`, `BigBufferScroll`) mueve el mapping y **respeta el
   clamp** del rango (`min_pos`/`max_pos` fijados por `begin`).
5. Rechaza un mundo más pequeño que el viewport.

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/field/068_double_buffer_scroll
```
