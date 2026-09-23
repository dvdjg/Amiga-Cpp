# HOST-045 — `Surface::fill_polygon` (polígono convexo por scanline)

Rasterizado CPU de un polígono convexo por scanline sobre un `Playfield` de prueba
(layout interleaved idéntico a `CanvasPlayfield`).

## Qué cubre

- Geometría: puntos dentro/fuera del polígono rellenos/limpios.
- Varios triángulos y el recorte (**clip**) de la superficie.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/field/045_surface_polygon
```
