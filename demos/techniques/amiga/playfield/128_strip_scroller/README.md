# 128 — scroller por tiras (Copper ring + incoming strip)

Referencia del **camino rápido de scroll**: **50 fps (1 campo)** en A500 single-playfield (5 planos),
CPU baja en el hotpath. Usa el camino implementado en `eng/field/strip_scroller.hpp` +
`strip_composer.hpp` (ver `docs/engine/architecture/SCROLL_VARIANTS.md §4.1`).

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/128_strip_scroller --release
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/128_strip_scroller
```

## Qué hace

- Anillo de **2 pantallas** (43 words) + guarda/fetch; viewport 320×256; 5 planos interleaved.
- Banco de **16 tiles** de 16×16×5 (color sólido) y un **mapa** que se repite (32 columnas).
- Por frame (`kStepX = 2` px): `plan_strip_frame` decide la tira; si cruza frontera de tile,
  `compose_column` la compone desde los tiles (**separados**, tile a tile) y `blitter_strip_column`
  la pinta en la guarda; `strip_copper_values` + `StripComposer::patch` parchean la copperlist
  (`BPLCON1` fine + `BPLxPT` por plano) sin re-emitir.

## Puntos clave (lecciones)

- **`BPL1MOD` depende del ancho FETCHEADO** por el display (~21 words), no del ancho del anillo:
  `BPL1MOD = planes*ring_w_bytes − fetch_words*2`. Con la fórmula "completa" el display lee mal el
  interleave (aparecen rayas en vez de tiles). Ver `StripScrollGeometry::bpl_mod`.
- **`BPLCON1` (fine) usa la convención canónica** `fine_delay(x) = (16 − (x&15)) & 15` y el coarse
  `(x−1) & ~15` (`playfield_scroll.hpp`). Con el fine "directo" (`x & 15`) el fondo **salta 16 px** al
  cruzar palabra (trompicones); con la canónica `visible_left = x` (movimiento suave).
- Los tiles del mapa **no se asumen contiguos**: `compose_column` los copia tile a tile.
- `BLTSIZE` tiene H de 10 bits (máx 1024): la columna se parte en `column_blits` (256×5 = 1280 → 2).

## Límites

- Single-playfield 5 planos. El **DPF 3+3** satura el bus (≤25 fps); ver `BUS_BUDGET.md`.
- El mapa es un patrón repetido; un mapa largo necesita stream de tiles por chunk.

## Validación visual (regla de oro: Ollama)

Medido: **49,92 fps** (1,002 campos/frame) en release. Validado con el modelo de visión local
(`node tools/analyze/ollama-desc.mjs ... `, sobre una **secuencia**, no una captura): movimiento
horizontal **suave, uniforme y continuo, sin saltos de 16 px ni huecos**. El `frame-diff`
(`tools/vision-review/frame-diff.mjs`) confirma cambio entre frames.
