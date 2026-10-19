# Demo 088 — Relleno 3D compuesto por bitplane (cubo rotando, 2 planos)

Demo de la vía **por plano** de la librería 3D (planificada en [`ROADMAP_3D_BLITTER.md`](../../../../../docs/guides/roadmap/ROADMAP_3D_BLITTER.md)): un cubo convexo rotando, con **cuatro colores sobre DOS bitplanes** (negro 0, azul 1 = plano 0, rojo 2 = plano 1, magenta 3 = ambos). El relleno lo hace `AmigaBackend::fill_polygons_by_plane`: una pasada por plano, con las aristas de las caras cuyo color tiene ese bit; la magenta entra en las dos pasadas.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes
node tools/debug/measure-fps.mjs 088_blit3d_planes --config A500_debug --duration 5000
```

## Técnica

- Display de 320x256 con **2 planos**, doble buffer y franja inferior de depuración en el mismo bitmap (nombre, versión y contador `F<num>` con `DiffText`).
- Por frame: `commit` (publica el buffer completo **al inicio** del update) → rotación `Rx·Ry·Rz` → transformación de los 8 vértices → culling por **normal 3D** (`mesh_traits::face_visible`, cámara en el origen, válido para cualquier convexo) → fast path de frustum (si el objeto entero está dentro, los 8 vértices se proyectan **una vez**) → por cara: clip de cámara (`clip_camera_polygon`) o proyección directa, cull de slivers/área, y `PlaneFillBuilder` + `fill_polygons_by_plane_dirty` (clear del rectángulo sucio, contorno ONEDOT por plano, dedupe de aristas compartidas, `area fill` FILL_XOR).
- **Rectángulo seguro del contorno** (lo que cierra el artefacto del borde): el trazador de líneas del Blitter escribe cada píxel **un paso por detrás**, así que una vertical dada en `x` se dibuja en `x-1`. El relleno por plano recorta cada cara a `[1, width-1] × [0, height-1]` antes de planificar las aristas (`plan_plane_edges_in_bounds`): el margen izquierdo absorbe el desfase, la última columna/fila es el último píxel incluido y el contorno queda **cerrado** en el borde (paridad par del `FILL_XOR`). Sin ese recorte, la vertical del borde izquierdo se dibuja en `x=-1` y, por el complemento a dos, en la cola de la fila anterior (`x≈width-1`), fuera del bbox de la cara: el clear por rectángulo sucio no la borra nunca (residuo). Lo fijan `HOST-482` (host) y `tests/amiga/l1_backend/003_plane_fill_bounds` (on-target, estático, con caras en `x=320`, `x=-1` e `y=232`).
- **Modo de validación `kDrift`** (`constexpr` en `src/main.cpp`, off por defecto): el cubo deriva lateralmente ±600 px y cruza los dos bordes. Es la evidencia de clip fino en hardware; con el rectángulo seguro no deja residuo.

## Búsqueda previa

- **Relleno por plano**: `eng::graphics::fill_polygons_by_plane` + `polygon_planes.hpp` (no se reimplementa el fill).
- **Cámara/clip/culling 3D**: `eng::graphics::Camera3D`/`clip_camera_polygon`/`screen_area2` + `mesh_traits::face_visible` (`render3d.hpp`, `mesh3d.hpp`).
- **Escena/display**: `scene::compose` + `scene::display` + `scene::palette`.
- **Texto de HUD**: `Surface::draw_text_words` + `DiffText`.

## Estado

**VERIFICADA en el alcance medido** (ver `src/VALIDATION.md`): 32.4 fps debug / 27.3 fps en modo deriva, pipeline correcto (culling general, fast path, dirty-rect) y borde limpio con el clip. **No cubierto**: bloqueo a 50 Hz, solapes entre sólidos (painter, sin z-buffer), la costura de 1 px de arista compartida entre planos (magenta) y la escritura fantasma de la fila 0 del trazador de líneas (defectos conocidos y separados).
