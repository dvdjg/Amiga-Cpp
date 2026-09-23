# HOST-066 — `route_camera` (cámara de ruta por fases)

Respalda `eng/scene/route_camera.hpp`: la posición es `eng::math::Vec<2, Fixed<s16,0>>`
(LONGITUD, píxeles) y los offsets de la circunferencia se generan con
`eng::SineTable<4096,64>` (el generador de seno del engine), no con tablas a mano.

## Qué cubre

- **Inicios de fase**: posiciones exactas de horizontal, vertical, diagonal, circular y
  senoidal, derivadas de la configuración por defecto.
- **Fases lineales**: +1 px/frame (el chipset solo desplaza 1 px mínimo por frame con
  BPLCON1, así que la suavidad depende de esto).
- **Círculo**: los puntos caen sobre la circunferencia de radio `radius_scale`
  (`|d² − r²|` dentro de la cota del redondeo entero del offset de la tabla).
- **Espejo** (`mirror_x`): refleja la X como `max_x + min_x − route_x`.
- **Modo salto** (a partir de `jump_start_frames`): la cámara se mantiene dentro de
  `[min_x, max_x] × [min_y, max_y]`.

## Nota

Al migrar las dos tablas de 64 entradas a `eng::SineTable` la ruta difiere de la versión
anterior como máximo **1 px**, y solo en las fases circular/senoidal (el resto es idéntico);
la demo `101_ehb_tile_scroll_driver` lo cubre en hardware.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/066_route_camera
```
