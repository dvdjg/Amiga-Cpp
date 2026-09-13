# Test HOST-024: offset de parallax "soft DPF"

Respalda `eng::field::parallax_pattern_offset_px`
(`engine/include/eng/field/xlimited.hpp`), la parte pura de la técnica de fondo
RoboCod ("soft DPF"): un playfield single de 5 planos donde el 5.º (fondo) se
copia por Blitter con un offset de contenido para scrollear a `1/div` del FG.

El plano de fondo comparte el scroll del FG (el display lee el bitmap desde
`camx`), así que el patrón debe desfasarse `-camx*(div-1)/div`; la posición
aparente en pantalla es `src + camx`. Se comprueba:

- `div=1`/`div=0` no desfasan (config sin parallax).
- Con paso de cámara múltiplo de `div`, el offset es entero **exacto** y el fondo
  avanza 1 px por frame (sin saltos de columna cada 8/16 px).
- El reparto `word (16 px) + shift (0..15)` que consume el barrel shifter del
  Blitter en `XLimitedPlayfield::make_bg_plane_copy_job`.

```bash
bash tools/run-host-tests.sh tests/host/024_parallax_offset
```

Contexto: `docs/reference/amiga/techniques/robocod-layered-scroll.md`.
