# HOST-061 — `map_flat_scroll` (mapper de virtual playfield)

Valida `eng::field::map_flat_scroll` (`engine/include/eng/field/amiga_display_mapper.hpp`), la
traducción **neutral** de la cámara de un bitmap flat a los registros del display (fetch ancho
`DDFSTRT=$30`): `planeaddx`, `planeaddy`, `BPLCON1` y `BPLMOD`. Aísla el mapper de la superficie y
del Copper (modelo objetivo §5 de `PLAYFIELD_SCROLL_ARCHITECTURE.md`).

## Qué fija

- `BPLCON1 = (16-fine)&15` duplicado en ambos nibbles.
- `planeaddx = ((cam_x-1) & ~15)/8` (una word antes solo cuando `fine==0`); propiedad continua en todo el rango.
- `planeaddy = cam_y * planes * row_bytes` (interleaved).
- `BPLMOD = row_bytes*planes - fetch_bytes` (42 con `$30`).
- `cam_x < 1` se clampa a 1 (con el fetch adelantado el puntero apuntaría antes del bitmap).

## Cómo corre

```bash
bash tools/run-host-tests.sh tests/host/field/061_flat_mapper
```

## Relación

- Estrategia de cámara: `BigBufferScroll` (HOST-044).
- Consumidor: `eng::field::FlatScrollPlayfield` (`flat_playfield.hpp`), verificado por la demo
  `demos/amiga/120_virtual_playfield`.
- Fórmula de referencia: `engine/include/eng/graphics/drivers/tile_scroll.hpp`.
