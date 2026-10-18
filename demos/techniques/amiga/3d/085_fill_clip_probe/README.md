# 085_fill_clip_probe — sonda de recorte 2D + relleno por plano (WIP)

Sonda **mínima y aislada** del camino común de las demos 3D poligonales: `retro::clip_polygon` (recorte 2D) + relleno por plano del Blitter (`AmigaBackend::fill_polygons_by_plane`). Nace del protocolo de etapas (`PROTOCOLO_ETAPAS_GRAFICOS.md`): en vez de lanzar la escena completa una y otra vez, pocos elementos con un caso límite explícito (triángulo que cruza el borde, pentágono medio fuera, cuadrado de referencia) y verificación por pasos.

## Hallazgos que ha producido

1. **`clip_polygon_pass` perdía la arista de cierre** (lib2d.hpp): recorría n−1 aristas y lo parcheaba con un vértice duplicado, de modo que un polígono con el primer vértice FUERA devolvía polígonos incompletos o < 3 vértices con parte visible dentro. **Corregido** con barrido con wrap-around; regresión fijada en **HOST-479**.
2. **Convención de bordes del recorte**: con la semántica semiabierta de `clip_inside` (`x < maxX`), el rect debe usar la **última columna/fila incluida** (`width−1`/`height−1`); pasar `width`/`height` deja los vértices de corte fuera del buffer y el contorno del Blitter corrompe la palabra vecina.

## Uso

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/085_fill_clip_probe --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/085_fill_clip_probe --sequence-frames 8 --sequence-interval-ms 350
# A/B contra la referencia CPU:
printf 'DEMO_OPT=-O2\nDEMO_FLAGS=-DCLIP_PROBE_CPU\n' > demos/techniques/amiga/3d/085_fill_clip_probe/build.args
```

Estado: **WIP** — quedan artefactos en el camino Blitter bajo aislamiento (comparación píxel a píxel CPU vs Blitter en curso).
