# HOST-211: render de malla n-gon sobre `Surface`

Test host de `eng::graphics::mesh_render_poly_filled` (`eng/graphics/mesh_renderer.hpp`):
el pipeline de alto nivel para efectos 3D de caras sólidas, sin funciones de bajo nivel en
el llamador.

## Qué comprueba

1. **Transform** al mundo con `mesh_transform` (`Affine<3,Coord,Coord>`).
2. **Culling + orden** por normal almacenada (`mesh_patches_order_lit`, `ConvexPatchesLit`):
   de un cubo con la cámara fuera de eje se ven exactamente **3 caras**.
3. **Proyección** en perspectiva (`project_perspective`, genérica sobre el escalar).
4. **Relleno por cara** con `Surface::fill_polygon(xs, ys, count, color)` (n-gon, no
   triángulos): el centro del cubo queda pintado, las esquinas no, y aparecen exactamente
   3 colores de cara.

## Salida de referencia

```
OK: mesh_render_poly_filled (cubo n-gon: transform, culling, proyeccion y relleno por cara).
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/211_mesh_poly_render
```
