# HOST-095 — colisión 2D

Respalda `engine/include/eng/core/util/collision.hpp`.

## Qué cubre

- **AABB**: `aabb_overlap`, `aabb_contains`, `point_in_aabb` (bordes exclusivos).
- **Orientación** (`orient`, con `muls.w`) y **segmentos**: `segments_intersect`
  (incluye colinealidad/contacto).
- **Triángulo**: `point_in_triangle` (incluye bordes).
- **Círculos**: `circle_overlap` comparando distancias al cuadrado (sin `sqrt`).

## Límites

Coordenadas `s16` con diferencias que también caben en `s16` (espacio pantalla/tile):
así los productos y las distancias al cuadrado caben en `s32` sin promoción a 64 bits
(`__muldi3`). Todo entero y sin división.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/core/095_collision
```
