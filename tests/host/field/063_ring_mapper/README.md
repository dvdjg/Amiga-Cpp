# HOST-063 — `map_ring_scroll` (mapper del corkscrew/XYLimited)

Valida `eng::field::map_ring_scroll` (`engine/include/eng/field/amiga_display_mapper.hpp`), la
traducción **neutral** de la cámara del corkscrew a los registros del display: `planeaddx`, `BPLCON1`
(incluidos los bits de fetch ancho `0x4400`/`0x8800`), offset vertical del anillo
(`display_offset`/`planeaddy`) y **split** (`split_line`/`split_active`).

Es la fórmula extraída de `XLimitedPlayfield::hardware_view()` (réplica de `UpdateCopperlist`,
`xlimited.c`), templada en `display_height` para conservar el módulo compile-time (`fast_div`, sin
`__umodsi3`). Con `DisplayHeight = 0` cae al `display_h` runtime (se comprueba la equivalencia).

## Qué fija

- `planeaddx` y `BPLCON1` para varios `videoposx`, incluido el camino de fetch ancho (`I=32`).
- `display_offset = (videoposy + tile_h) % display_height`, `planeaddy` y el wrap (vy negativo/272).
- `split_line = display_height - display_offset` y `split_active` (frontera 256, modo lineal, sin Y).

## Cómo corre

```bash
bash tools/run-host-tests.sh tests/host/field/063_ring_mapper
```

## Relación

- Consumidor: `XLimitedPlayfield::hardware_view()` (`xlimited.hpp`), verificado por las demos
  107/110/201/202.
- Mapper del virtual playfield (flat): HOST-061.
