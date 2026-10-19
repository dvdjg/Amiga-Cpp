# Demo 088 — Relleno 3D compuesto por bitplane (pila 3D, paso A: colores)

Primer paso de la vía **por plano** de la librería 3D (la diseñada en [`ROADMAP_UNIFICADO.md`](../../../../../docs/guides/roadmap/ROADMAP_UNIFICADO.md) §«Relleno de polígonos compuesto por bitplane» y planificada en [`ROADMAP_3D_BLITTER.md`](../../../../../docs/guides/roadmap/ROADMAP_3D_BLITTER.md)): un cubo isométrico estático con **tres caras visibles y cuatro colores sobre DOS bitplanes** (negro 0, azul 1 = plano 0, rojo 2 = plano 1, magenta 3 = planos 0 y 1). El relleno lo hace `AmigaBackend::fill_polygons_by_plane`: una pasada por plano, con las aristas de las caras cuyo color tiene ese bit; la magenta entra en las dos pasadas.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes
```

## Técnica

- Display de 320×256 con **2 planos** y paleta de 4 colores; franja inferior de depuración en el mismo bitmap (nombre, versión y contador `F<num>` con `DiffText`).
- Tres caras del cubo (izquierda, superior, derecha) en coordenadas isométricas (sin aristas horizontales); `graphics::PlaneFillBuilder<3>` + `AmigaBackend::fill_polygons_by_plane` (contorno XOR ONEDOT por plano, dedupe de aristas compartidas con `plan_plane_edges`, area fill `FILL_XOR` descendente, todo ya verificado por 203/077).
- Sin rotación ni clipping todavía: este paso aísla el **color multi-plano**.

## Búsqueda previa

- **Relleno por plano**: `eng::graphics::fill_polygons_by_plane` + `polygon_planes.hpp` (no se reimplementa el fill).
- **Escena/display**: `scene::compose` + `scene::display` + `scene::palette` (patrón de la 203).
- **Texto de HUD**: `Surface::draw_text_words` + `DiffText`.

## Estado

**PASO A — color compuesto por plano y relleno OK; PASO B (rotación) EN CURSO.** El relleno por plano es correcto (validado por la sonda estática `089_blit3d_fill_probe`: rectángulo y rombo inclinado perfectos) y el color multi-plano también (azul/rojo/magenta con áreas iguales por cara en el muestreo de píxeles de `VALIDATION.md`). El artefacto que se veía era de **publicación del doble buffer a mitad de update** (el fill escribe de abajo arriba y la captura podía pillar el blit a medias): corregido publicando al inicio del update; queda un caso residual intermitente que se cerrará publicando en el VBlank. La **rotación 3D** está en implementación (transform `Rx·Ry·Rz` + clip/proyección de `render3d` + culling por normal 3D); el culling aún deja ver una sola cara por frame (signo del winding pendiente de corregir), así que el paso B **no está verificado**. Detalle: `VALIDATION.md`.
