# Demo 091 — Scene3D retenida: jerarquía, rotación y materiales

Tutorial de la **fachada 3D de juego** (`eng::scene3d`, G0–G3 de [`ROADMAP_3D_GAME_API.md`](../../../../../docs/guides/roadmap/ROADMAP_3D_GAME_API.md)): un cubo raíz y un cubo hijo viven en una `Scene<8, 8>` con handles generacionales; el raíz gira en tres ejes y el hijo, ligado con `set_parent`, hereda esa rotación mientras gira sobre sí mismo. Los materiales (color por cara) están en una `ModelTable`; el render por defecto es alambre para separar la escena del coste del relleno de caras.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/091_scene3d_nodes --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/091_scene3d_nodes
# Secuencia para visión:
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/091_scene3d_nodes \
  --settle-ms 1500 --sequence-frames 4 --sequence-interval-ms 400
```

## Técnica

- **Escena**: `eng::scene3d::Scene<8, 8>` (8 nodos, 8 vértices por malla, 64 aristas de alambre). `spawn` crea los nodos con `NodeDesc`; `set_parent` liga el hijo al raíz; `set_rotation` fija matrices Q4.12; `Scene::render_wire` resuelve la jerarquía con `update_transforms` (solo nodos sucios) y dibuja aristas únicas por nodo.
- **Modelos**: `eng::scene3d::ModelTable<2, 8, 24, 6, 2>` registra dos cubos `constexpr` de `retro::mesh_primitives` con `add_mesh` (material 0: color 3/5). La variante `K091_WIRE=0` usa `Scene::render(ModelTable, ...)`, que toma color y patrón del material por cara y los pasa como `FaceStyle` al relleno.
- **Cámara**: `Camera3D<eng::coord>` con focal 340, near 64 y viewport 320×208; la cámara está en el origen y mira hacia `+z`, como el resto de `render3d`.
- **Alambre por Blitter**: `m_canvas.set_rasterizer(&kBlitterRaster)`; `render_wire` pasa un `FramePlan` a `Surface::draw_line`, que encola una `BlitJobKind::Line` por plano (color 15 → 4 planos) y ejecuta el plan con `backend.execute_frame_plan`. `ONEDOT` deja una arista por fila; las aristas compartidas no se duplican en la lista.
- **Doble buffer**: `composition::Scene` con 2 buffers; `commit()` al inicio de `update` publica la página completa anterior y el dibujo va a la trasera. Las dos páginas se completan antes de `mark_ready`.
- **HUD**: `eng::debug::HostHud` escribe nombre, versión, frame, fps y telemetría (`E<aristas> J<jobs> OK`) en el overlay del host. La franja inferior del bitmap lleva un rótulo estático de refuerzo.
- **Infraestructura de display**: `composition::Scene` + `PolygonFillService` (máscara 1 bpp en Chip) como en las demos 087/090; la fachada de juego aún no instala ese sink en `App`.

## Búsqueda previa

- **Escena/nodos/handles**: `eng::scene3d::Scene` y `util::Pool` (`engine/include/eng/api/3d.hpp`, `core/util/pool.hpp`); no hay escena 3D retenida en `scene::World` (2D).
- **Mallas/materiales**: `ModelTable`/`Model3DAssetView` (G1) y `retro::mesh_primitives` para los cubos; no se escriben vértices a mano.
- **Render alambre**: `eng::graphics::render_wire` + `clip_camera_segment` (P7), no `mesh_render_wire` (sin near clip).
- **Relleno/referencia**: `PolygonFillService` + `Playfield::set_polygon_fill_sink` + `AmigaBackend::blitter_fill_polygon_strided` para la variante rellena; `scene::compose`/`display`/`palette` para el display.
- **HUD**: `eng::debug::HostHud`, `eng::util::TextLine`, `Surface::draw_text_words`.

## Estado

**Funcional y con visión OK** (`src/VISION.md`): dos cubos de alambre blancos, contornos limpios, sin halos ni restos; el hijo cambia de posición/orientación con la rotación del padre. **Presupuesto ABIERTO**: 7,85 fps (6,37 campos/frame) con 24 aristas × 4 planos = 96 jobs de línea; el coste dominante es el arranque/espera por job del Blitter, no el transform. La ruta CPU de `draw_line` mide 2,97 fps y la variante rellena por cara 6,28 fps con tres cubos (medida previa). La deuda de cadencia queda para la campaña P8/P5; esta demo no se declara VERIFICADA por fps. Medidas, alcance y prompts: [`VALIDATION.md`](VALIDATION.md).


Actualizacion: la variante rellena (K091_WIRE=0) ya renderiza cubos solidos tras corregir el DDA de convex_spans (avance de varios px por fila en aristas poco profundas); HOST-045/HOST-481 cubren la regresion. La medida actual es 8,39 fps / 5,96 campos con 2 cubos y doble buffer, aun por debajo del suelo de 25 fps de P8.

A/B de P8: con K091_SILHOUETTE=1 la escena rellena usa una silueta convexa por objeto (1 fill por objeto, 4 planos) y mide 12,72 fps / 3,93 campos (557.902 ciclos), frente a 8,39 fps del relleno por cara. Sigue por debajo de 25 fps; falta la composicion S + M_p y el router de coste.
