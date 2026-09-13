# Test HOST-024: offset de fondo (parallax/fijo) y compensación de split

Respalda la parte pura de la técnica de fondo RoboCod ("soft DPF")
(`engine/include/eng/field/xlimited.hpp`): un playfield single de 5 planos donde el
5.º (fondo) se copia por Blitter con un offset de contenido.

El plano de fondo comparte el scroll del FG, así que el contenido hay que
desfasar `src = bg_x - scroll_x`; la posición aparente es `src + scroll_x`.

- `parallax_pattern_offset_px(camx, div, period)`: fondo a `1/div` (`bg_x=camx/div`).
- `fixed_bg_offset_px(camx, period)`: fondo **FIJO** (`bg_x=0` → `src=-camx`), cancela
  TODO el scroll (coarse+fine).
- **Compensación del Copper split**: las dos mitades (arriba/abajo del corte)
  muestrean filas **contiguas** del patrón (`src_y=0` y `src_y=split`), de modo que
  el fondo queda continuo y fijo aunque el FG haga wrap vertical. El reparto en dos
  rects se hace con `make_bg_plane_copy_rect_job`.

```bash
bash tools/run-host-tests.sh tests/host/024_parallax_offset
```

Contexto: `docs/reference/amiga/techniques/robocod-layered-scroll.md` §3.
