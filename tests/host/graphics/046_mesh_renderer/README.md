# HOST-046 — `mesh_renderer` (malla 3D → Surface)

Render de una malla 3D a una `Surface`: proyección en perspectiva, back-face culling
(`mesh_painter_order`) y relleno de las caras visibles vía `Surface::fill_polygon`.
`Playfield` de prueba en RAM, sin hardware.

## Qué cubre

- Proyección de vértices y orden de pintado (painter's algorithm).
- Culling de caras ocultas y relleno del polígono resultante.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/046_mesh_renderer
```
