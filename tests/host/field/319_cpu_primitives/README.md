# HOST-319 — Primitivas CPU optimizadas (rect, línea, polígono)

## Qué cubre

`engine/include/eng/field/cpu_primitives.hpp` (portable, sin chipset):
- `cpu_fill_rect`: relleno de rectángulo por **spans** (una fila = un tramo), sin píxel a píxel.
- `cpu_line`: línea con **Bresenham agrupado en spans por fila** (misma cobertura que Bresenham,
  `draw_span` por fila en vez de un píxel a uno). Para líneas mayormente horizontales reduce de
  `|dx|` a `|dy|+1` escrituras.
- `cpu_fill_polygon`: polígono convexo por **even-odd** con `draw_span` por fila.

## Invariantes

1. `cpu_line` **cubre** todos los píxeles del Bresenham de referencia (misma cobertura).
2. `cpu_fill_rect` pinta exactamente `h` filas dentro de `[x, x+w)`; fuera queda intacto.
3. `cpu_fill_polygon` rellena el interior (even-odd) sin agujeros y sin tocar fuera.

## Por qué importa

El dibujo CPU píxel a píxel (`write_pixel`) es demasiado lento para uso real. Estas rutinas son el
**fallback CPU canónico** del rasterizador (frente a la ruta Blitter del Amiga) y la base portable
para plataformas sin Blitter (p. ej. el futuro port a **Atari ST**). `CpuRaster::draw_line` usa
`cpu_line`; `fill_rect` ya va por spans.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/field/267_cpu_primitives
```

## Relación

- Diseño: `docs/engine/architecture/RASTER.md`; protocolo: etapa 2/4 de
  `docs/guides/methodology/PROTOCOLO_ETAPAS_GRAFICOS.md`.
- Ruta acelerada de referencia: `BlitterRaster`/`Surface::blit` (HOST-212/218/260/268).
