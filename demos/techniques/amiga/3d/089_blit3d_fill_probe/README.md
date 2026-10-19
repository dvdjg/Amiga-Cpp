# Demo 089 — Sonda del relleno por plano (diagnóstico)

Sonda estática de diagnóstico del relleno compuesto por bitplane: dibuja **una sola vez en `init`** un rectángulo (control con aristas rectas) y un rombo (aristas inclinadas) sobre un display de **un plano** (el plano de trabajo es la imagen visible), usando el mismo camino que la demo 088 (`graphics::PlaneFillBuilder` + `AmigaBackend::fill_polygons_by_plane`: contorno XOR ONEDOT + dedupe + area fill `FILL_XOR` descendente). El resto de la vida de la demo no toca la pantalla, así que una captura **no puede** pillar el blit a medias.

Sirve para separar «bug del fill/contorno» de «artefacto de captura/tearing»: con el resultado limpio queda demostrado que el fill por plano es correcto también con cruces inclinados.

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/089_blit3d_fill_probe --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/089_blit3d_fill_probe
```

## Búsqueda previa

- **Relleno por plano**: `eng::graphics::fill_polygons_by_plane` + `polygon_planes.hpp`; **escena/display**: `scene::compose`/`display`/`palette` (patrón de la 203); **texto**: `Surface::draw_text_words`.

## Estado

**OK como sonda**: rectángulo y rombo completamente rellenos (evidencia: `out/blit3d/089probe.png` y `src/VISION.md`). Es la prueba que descarta el bug del fill en la 088 (el artefacto de allí era de publicación mid-frame). Demo de diagnóstico: no forma parte de la pila de efectos.
