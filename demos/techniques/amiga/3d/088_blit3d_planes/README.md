# Demo 088 — clipping 3D y relleno por plano

La demo prueba el recorte de un cubo convexo contra la cámara, el descarte de caras traseras y el relleno de sus colores por bitplane. El modo estático es determinista para comparar poses con la referencia CPU; el modo animado opcional rota el cubo y lo desplaza por los cuatro lados y esquinas del viewport.

## Ejecución

Build estático O1, esquina superior izquierda:

```bash
DEMO_FLAGS="-DK_CLIP_CASE=6 -DK_CLIP_ORIENTATION=1 -DK_CLIP_BUFFERS=1 -DK_CLIP_REPEAT=0" bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes
```

Build animado con doble buffer y secuencia de validación:

```bash
DEMO_FLAGS="-DK_CLIP_ANIMATE=1 -DK_CLIP_REPEAT=0 -DK_CLIP_BUFFERS=2" bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --settle-ms 1000 --sequence-frames 24 --sequence-interval-ms 500
```

## Variantes

`K_CLIP_CASE=0..12` selecciona centro, cuatro laterales, cuatro esquinas o cuatro poses totalmente fuera. `K_CLIP_ORIENTATION=1` fija una inclinación de 45° para mostrar varias caras. En modo estático, `K_CLIP_BUFFERS=1` renderiza una página y `K_CLIP_BUFFERS=2` prepara dos páginas; `K_CLIP_REPEAT=1` vuelve a pintar la pose fija. `K_CLIP_ANIMATE=1` activa la rotación y la trayectoria de cámara; exige dos páginas y su valor predeterminado es `K_CLIP_BUFFERS=2`. `K_CLIP_PROJECT_BEFORE_SCREEN_CLIP=1` (predeterminado) proyecta una vez los ocho vértices cuando todos permanecen delante de `near_z` y deja que el backend recorte las caras al rectángulo seguro; con `0`, cada cara usa el clipping completo de cámara como referencia A/B. `K_CLIP_HUD_UPDATE_SHIFT=3` (predeterminado) refresca el contador en buckets de ocho frames y mantiene cada página de doble buffer sincronizada; `0` actualiza el contador en cada frame.

## Técnica

- Display OCS de 320×256 con dos bitplanes: negro = 0, azul = 1, rojo = 2 y magenta = 3. La franja inferior muestra nombre, versión, pose o trayectoria, modo de buffer y contador.
- HOST-481 valida `Camera3D` contra near y los cuatro planos laterales. En la trayectoria acotada de esta demo, la ruta rápida proyecta los ocho vértices una vez y delega el recorte lateral al rectángulo seguro del backend; si algún vértice cruza near, vuelve al recorte completo de cámara. El culling por normal descarta las caras traseras y `PlaneFillBuilder` agrupa los contornos por bitplane.
- El backend rellena cada plano mediante contorno ONEDOT/EOR y `FILL_XOR`. Su rectángulo seguro es `[1,width-1] × [0,height-1]`; la referencia CPU admite la diferencia de rasterización documentada de ±2 px. La interpolación de `cut_edge` canoniza el sentido dentro→fuera para que dos caras que comparten una arista obtengan el mismo corte aunque recorran la arista en sentidos opuestos.
- El modo animado mantiene una sola trayectoria rectangular cerrada por los cuatro lados y actualiza los tres ángulos por suma; el doble buffer evita mostrar una página mientras se rellena.

## Aserciones de secuencia

Para verificar el ciclo completo y las cuatro esquinas, captura 32 muestras espaciadas y ejecuta `node tools/analyze/verify-088-sequence.mjs out/run/088_blit3d_planes/A500_debug/sequence --screen-x 76 --screen-y 36 --require-coverage`; el gate comprueba cada borde/esquina, presencia de los tres colores, cambios por muestra y ausencia de rojo/azul en el HUD. Para transiciones consecutivas, captura `--sequence-step-frames 8` y añade `--consecutive`; `node tools/vision-review/flicker-check.mjs --demo demos/techniques/amiga/3d/088_blit3d_planes --frames 8` complementa el gate determinista.

## Búsqueda previa

- **Relleno por plano**: `eng::graphics::fill_polygons_by_plane` y `polygon_planes.hpp` (se reutiliza el relleno existente).
- **Cámara, clipping y culling 3D**: `eng::graphics::Camera3D`, `clip_camera_polygon`, `screen_area2` y `mesh_traits::face_visible` (`render3d.hpp`, `mesh3d.hpp`).
- **Escena y display**: `scene::compose`, `scene::display` y `scene::palette`.
- **HUD**: `Surface::draw_text_words` y `DiffText`.

## Estado

El alcance medido, los prompts de visión, las respuestas crudas, FPS/ciclos y las variantes fuera de pantalla están en [`VALIDATION.md`](VALIDATION.md). La validez geométrica se confirma para el cubo y las poses enumeradas; no implica una garantía universal para cualquier polígono, escala, malla o frecuencia de actualización.
