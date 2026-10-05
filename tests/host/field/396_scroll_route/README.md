# HOST-396 — `scroll_route`

`eng/field/scroll_route.hpp` (`playfield::ScrollRoute`): ruta de scroll **continua** para demos.

## Qué comprueba

Sobre 3 ciclos completos de las 5 fases (horizontal, vertical, diagonal, circular, Lissajous):

- **Cada frame mueve <= 1 px por eje** (|vx|, |vy| <= 1): los motores con `max_step` pequeño
  (p. ej. el corcóscru) rechazan pasos mayores.
- **Cada frame mueve al menos un eje** (dos frames seguidos nunca son la misma posición → cada frame
  es una imagen distinta).
- La **Y** se mantiene en `[0, YMax]` y recorre todo el rango (con rebote).

## Por qué

`RouteCamera` (demo 101) cuantiza el ángulo a 1/64 de vuelta → 8 frames por paso (el mismo píxel);
y las rutas por posición absoluta por fase **saltan** al cambiar de fase. Esta ruta avanza por
**velocidad** (sin saltos) con una tabla de seno de 256 muestras que avanza cada frame.
