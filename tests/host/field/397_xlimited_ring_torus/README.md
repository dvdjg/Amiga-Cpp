# HOST-397 — toro del anillo XYLimited (demo 205)

Con la geometría real de la demo 205, la traducción de la cámara del corcóscru a los registros del
display a lo largo de todo el recorrido vertical, y la invariante de cierre del anillo.

`eng/field/amiga_display_mapper.hpp` (`playfield::map_ring_scroll`), con anillo `display_height = 320`,
viewport 208, tile 16, 3 planos, fila 44.

## Qué comprueba

- **Recorrido Y completo** (`320 − 208 = 112` px de anillo de 20 tiles): `display_offset` avanza 1 px
  por `vy` (offsets distintos → 0 frames repetidos por el eje Y), y el **split** entra cuando el
  `split_line` cae dentro de la ventana (`vy = 96` frontera sin split; `vy = 112` con split activo
  `192 < 208`).
- **Cierre del toro**: un periodo (320) devuelve al mismo offset (16) sin costura.

## Por qué

Es la invariante que arregló el “mapa roto” de la 205: los **tiles del anillo** (`320/16 = 20`) deben
**dividir la altura del mapa** (40) para que el anillo cierre al envolver. Con anillo 288 (18 tiles, no
divide 40) la banda inferior leía filas equivocadas del toro. El mapper en sí
(`planeaddx`/`BPLCON1`/offset/split/negativos) lo cubre HOST-063; aquí se fija la geometría concreta y
la invariante de divisibilidad.
