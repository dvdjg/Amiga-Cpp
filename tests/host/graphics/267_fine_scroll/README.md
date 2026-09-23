# HOST-267 — Scroll horizontal fino (`effects::FineScroll`)

Valida `eng::effects::FineScroll` (`eng/api/effects.hpp`), el helper reusable de **scroll fino
de 1 px/frame** promovido desde la demo 210 (`demos/amiga/210_copper_blitter`):

- **`attach`** rechaza configuración inválida (`plane == nullptr`, `rows == 0`, `visible_words == 0`)
  y fija la columna inicial en `visible_words`.
- **Geometría**: `row_bytes() = (visible_words + 1) * 2` (guarda + columna entrante) y
  `DDFSTRT = $30` (fetch de 1 word extra); el helper hace *shift* de buffer, no *ring wrap*.
- **Cadencia 1 px/frame**: `step()` es `false` 15 veces y `true` en el cruce de word (cada 16
  pasos); `column()` avanza exactamente 1 por cruce.
- **`BPLCON1 = (16 − fine) & 15`** y su wrap tras el cruce.
- **`BlitJob`**: desplazamiento (`src = plane+1`, `dst = plane`, `words_per_row = visible_words`,
  módulos `2`) y columna entrante (`src = col`, `dst = plane+visible_words`, `words_per_row = 1`,
  módulo de `D = row_bytes − 2`).
- **Regresión cruzada**: `soft_dpf::bg_shift_for(x).shift == fine_delay(x)` y
  `xlimited::kDdfStrt == fine_scroll_ddfstrt`, para que los consumidores del fine delay no
  diverjan de `playfield_scroll.hpp`.

El patrón del buffer lo genera el llamador (procedural o tilemap); para contenido periódico con
*ring wrap* real, el driver es `eng/graphics/drivers/tile_scroll.hpp`.

## Ejecución

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/267_fine_scroll
```

Referencias: `docs/reference/amiga/techniques/README.md` (scroll fino `BPLCON1`/`DDFSTRT`),
`demos/amiga/210_copper_blitter/README.md`.
