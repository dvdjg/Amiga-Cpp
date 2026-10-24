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
- **Alambre directo por Blitter**: `Scene::collect_wire_edges(...)` proyecta las aristas y las entrega a `retro::wire_lines(...)`, que fija el preludio del modo linea una vez y programa cada arista con `blitter_line_prepare`/`blitter_line_draw` (sin `FramePlan`/`Surface::draw_line`). La ruta antigua por plan queda solo como A/B.
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

**Funcional y con visión OK** (`src/VISION.md`): dos cubos de alambre blancos, contornos limpios, sin halos ni restos; el hijo cambia de posición/orientación con la rotación del padre. **Cadencia verificada en la ruta directa**: 27,11 fps debug / 30,57 fps release (1,64 campos, 232.038 ciclos) con 1 plano y 18 aristas. La ruta por `FramePlan` se queda en 24,06 fps release; la CPU de `draw_line` mide 2,97 fps y la variante rellena 8,39 fps. Medidas, alcance y prompts: [`VALIDATION.md`](VALIDATION.md).


Actualizacion: la ruta directa `retro::wire_lines` mide **27,11 fps debug / 30,57 fps release** (261.644 / 232.038 ciclos; 1,82 / 1,64 campos) con 1 plano, culling de traseras y 18 aristas. Es el camino de juego recomendado; la ruta por `FramePlan` se queda en 22,07/24,06 fps y el lote de jobs no la mejora. La variante rellena mide 8,39 fps y la silueta convexa 12,64 fps; estas siguen por debajo de 25.

A/B de P8: con K091_SILHOUETTE=1 la escena rellena usa una silueta convexa por objeto (1 fill por objeto, 4 planos) y mide 12,64 fps / 3,96 campos (561.192 ciclos), frente a 8,39 fps del relleno por cara. Sigue por debajo de 25 fps; falta la composicion S + M_p y el router de coste.
