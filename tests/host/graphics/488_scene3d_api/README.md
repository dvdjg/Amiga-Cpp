# HOST-488 - Fachada 3D de juego, política numérica y modelo cocinado

Verifica `eng/api/3d.hpp` (G0 de [`ROADMAP_3D_GAME_API.md`](../../../../docs/guides/roadmap/ROADMAP_3D_GAME_API.md)): la escena retenida de capacidad fija con handles generacionales, cámara `Camera3D<eng::coord>`, alta/baja de nodos, posición/visibilidad, validación de cámara y render por `eng::graphics::render_solid` con painter entre nodos.

También cubre el primer tramo de G1: `eng::assets::Model3DAssetView` (chunk `Model3D` 15) valida un payload big-endian construido con `BlobWriter`, y `eng::scene3d::ModelTable` copia la malla/materiales a buffers propios y la pinta con la sobrecarga `Scene::render(ModelTable, ...)`. Se prueban los rechazos de payload truncado, índice fuera de rango, cara de 2 vértices, tabla llena y malla mayor que `MaxVertices`.

La prueba de render usa un `Playfield` de host que captura los polígonos delegados por `Surface` (el mismo borde que en Amiga usa el `PolygonFillSink` del Blitter). Comprueba que las caras caen dentro del viewport, que el nodo lejano se pinta antes que el cercano, que un nodo oculto no emite caras, que un cubo detrás del near no se pinta, y que se omiten las mallas fuera de rango o con más vértices que `MaxVertices`.

La sonda de política numérica fija la decisión de G0: `eng::coord` (`Fixed<s16,0>` en 68000) representa `focal=340`, `half_w=160` y `near=64`; `Q8.8` y `Q4.12` **no** representan `half_w=160` en `s16` (`from_int` desborda en runtime). Por tanto la cámara de la fachada usa q0 y la precisión sub-unidad de cámara queda para G8 (escala de píxel o LUT de `1/z`), no para esta fase.

El cruce con el cooker se completa con su self-test (`node dist/tools/assets/cook-model3d.js --self-test`), que empaqueta un cubo con el mismo layout y reparsea el UAF-R; el test C++ valida el mismo contrato desde el lado del runtime.

Alcance y límites: host puro, relleno CPU por `Surface`, sin Blitter, sin rotación, sin jerarquía padre/hijo, sin `Assets::model`, sin animación/proxies y sin medida de fps. La validación on-target, la visión y el presupuesto quedan para las demos de G2/G3; este test **no** declara verificada la fachada completa.

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/488_scene3d_api
```
