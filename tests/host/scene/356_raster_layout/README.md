# HOST-356 - raster_layout

Test de `eng/scene/display.hpp::RasterLayout` (composición de pantalla por **bandas**).

## Qué cubre

- `Band`: `bplcon0()` derivado de planos + bits de modo (`COLOR`/`DBLPF`/`HIRES`/`EHB`) y
  `modulo()` (interleaved vs bloques contiguos; `0` sin planos).
- `RasterLayout::materialize`: la banda 0 emite el display principal y las demás una
  `ModeSwitchZone` en su `top`. El caso probado es un **dual playfield 3+3** (6 planos, `DBLPF`)
  de 208 líneas + una **franja de 0 planos** (efectos *copper chunky*) con su propia paleta.
- Comprueba los dos `BPLCON0` emitidos (`0x6600` del campo, `0x0000` del tramo), el módulo y que
  la paleta del tramo llega a `COLOR00`.

## Cómo se ejecuta

```
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/scene/356_raster_layout
```
