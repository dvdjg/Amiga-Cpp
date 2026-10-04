# Referencia — `graphics/`

El módulo `eng::graphics` (`engine/include/eng/graphics/`) describe **memoria gráfica y trabajo de Blitter** con independencia del backend: bitmaps y vistas de plano, trabajos de blit, estado del Blitter, Copper y su planificación, y la composición de escena por capas.

```
   datos del juego                         engine::graphics                          hardware
  ─────────────────                       ──────────────────                        ────────
  dibujar una zona    ──►  BlitJob (blit_job.hpp)  ──►  FramePlan  ──►  backend  ──►  Blitter
  color de paleta     ──►  CopperIntent           ──►  copper::Plan ──► Scheduler ─►  Copper
  geometría del área  ──►  Bitmap / BitmapView
```

| Página | Qué documenta |
|---|---|
| [`planar.md`](planar.md) | `Bitmap`/`BitmapView`, `PlaneLayout`, `BlitJob`/`BlitPtr`, `BlitQueue`, constantes de `blitter_state.hpp`. |
| [`copper.md`](copper.md) | `copper::ListBuilder`/`Register`, `Plan`, `Scheduler`/`Timeline`, `DoubleBuffer`, `Template`, `StaticCopperList`. |
| [`composition.md`](composition.md) | `Scene`/etapas/`compose`/`limits`/`copper_chunky` y `frame_plan.hpp`. |
| `tilemap/` | `TileMap`/scroll por tiles, `TileEditor`, tabla de atributos, decodificación planar. |
| `effects/` | `Rotozoom`, `RasterGradient`, `PaletteCycle`/`PaletteTransition`. |
| `drivers/` | Drivers gráficos por estrategia (`tile_scroll`, `ehb_tile_scroll`). |

> Los subsistemas «planar» y «blitter» agrupan las cabeceras de nivel superior (`bitmap*`, `plane_layout`, `blit_*`, `blitter_state`, `bob`, `sprite*`); el resto son subdirectorios.

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
