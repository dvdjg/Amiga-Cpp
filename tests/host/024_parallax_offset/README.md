# Test HOST-024: offset de fondo (parallax/fijo) y compensación de split

Respalda la parte pura de la técnica de fondo RoboCod ("soft DPF")
(`engine/include/eng/field/xlimited.hpp`): un playfield single de 5 planos donde el
5.º (fondo) se copia por Blitter con un offset de contenido.

El plano de fondo comparte el scroll del FG, así que el contenido hay que
desfasar `src = bg_x - scroll_x`; la posición aparente es `src + scroll_x`.

- `parallax_pattern_offset_px(camx, div, period)`: fondo a `1/div` (`bg_x=camx/div`).
- `fixed_bg_offset_px(camx, period)`: fondo **FIJO** (`bg_x=0` → `src=-camx`), cancela
  TODO el scroll (coarse+fine).
- `bg_split_rects(d, display_h, viewport_h, bg_y)`: descompone el blit de fondo en
  **1 o 2 rectángulos** para compensar el Copper split; las dos mitades muestrean
  filas de patrón **contiguas** y reconstruyen la fila de pantalla (fondo fijo y sin
  costura en el corte). El demo emite los rects con `make_bg_plane_copy_rect_job`.
- `bg_window_for(camx, period, fetch_bytes)`: ventana horizontal del blit
  (`[planeaddx-2, planeaddx+fetch)` = guarda + visible) y `src_x` para dejar la
  imagen fija; reduce el blit de 25 a ~21 words/fila.
- `bg_shift_for(src_x)`: reparto word+shift del barrel shifter (`q - S == src_x`).

```bash
bash tools/run-host-tests.sh tests/host/024_parallax_offset
```

Contexto: `docs/reference/amiga/techniques/robocod-layered-scroll.md` §3.
