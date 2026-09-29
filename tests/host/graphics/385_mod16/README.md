# HOST-385 · mod16 (módulo de Blitter/Copper)

Test host de `eng::graphics::mod16`/`mod16u` (`eng/graphics/blit_job.hpp`): convierten un
**módulo** calculado en 32 bits (`row_bytes·planes − words·2`) al registro de 16 bits
`BLTxMOD`, comprobando el rango.

## Qué valida

- Valores reales **no potencia de 2**: `40−4=36`, `40−6=34`, `40·4−42=118`.
- Negativos (patrón solapado: `−words·2`).
- Extremos `s16` (`−32768`, `32767`).
- En host sin diagnóstico no hay aserción (el valor se trunca como el cast); el **trap de
  rango** se ejerce en m68k/build con `ENG_DEBUG`.

## Por qué

`static_cast<s16>` a ciegas truncaba en silencio un módulo fuera de rango. `mod16` centraliza
la regla y añade la aserción **sin coste en release** (`ENG_ASSERT`). **No** se restringe a
potencias de 2: los módulos reales no lo son y no hay división que ahorrar en el hot path.
