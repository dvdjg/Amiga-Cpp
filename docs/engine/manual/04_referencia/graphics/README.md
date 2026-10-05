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
| [`blitter.md`](blitter.md) | `Bob`/`BobDraw`/`BobErase`, `SpriteManager`/`SpriteConfig`, plantillas de sprite, `SpriteAllocator`/colisión, `Anim`. |
| [`palette.md`](palette.md) | `Palette`/`Color`/`ColorIndex` y `Palette32`/`PaletteWords`. |
| [`tilemap/`](tilemap.md) | `PackedTileCell`/dirty flags, `AttributeTable`, `TileEditor`. |
| [`effects/`](effects.md) | `Rotozoom`, `RasterGradientEffect`, `PaletteCycle`/`PaletteTransition`. |
| [`drivers/`](drivers.md) | `TileScrollScene<Mode>` y las convenciones de `playfield_scroll`. |

Otras cabeceras de nivel superior: `blit_job`/`blit_queue`/`blitter_state` (planar), `c2p`/`tile_planar` (conversión chunky↔planar), `pattern_fill`/`polygon_planes`/`mode_switch`/`raster_intent`, fuentes (`font5x7`/`font8`/`glyph_cache`) y `mesh_renderer`.

> Los subsistemas «planar» y «blitter» agrupan las cabeceras de nivel superior (`bitmap*`, `plane_layout`, `blit_*`, `blitter_state`, `bob`, `sprite*`); el resto son subdirectorios.

Volver a [Referencia](../README.md) · [índice del manual](../../README.md).
