# External Scroll/Tilebuffer Source Map (ACE + amiga-stuff)

Objetivo: dejar trazabilidad explicita de formulas y patrones importados al engine.

## Origen 1: ACE scrollbuffer (MPL-2.0)

Fuente:

- `C:/Users/dvdjg/Documents/programa/AI/Amiga-C++/ACE/src/ace/managers/viewport/scrollbuffer.c`

Elementos importados:

1. Fine shift horizontal:
   - `uwShift = (16 - (uwScrollX & 0xF)) & 0xF`
   - empaquetado PF1|PF2: `uwShift = (uwShift << 4) | uwShift`
2. Coarse pointer horizontal:
   - `ulBplAddX = ((uwScrollX - 1) >> 4) << 1` (en ACE se ajusta por contexto hires)
   - en engine reusable se normaliza como `((x >> 4) << 1)` para evitar salto de borde en x=16.

Destino en engine:

- `engine/include/engine_external_scroll.h`
- `engine/src/external_scroll.c`
- `engine/src/view.c`

## Origen 2: amiga-stuff xyunlimited2 (referencia local)

Fuente:

- `C:/Users/dvdjg/Documents/programa/AI/amiga-stuff/scrolling_tricks/xyunlimited2.c`

Elementos importados:

1. Scheduler de carga distribuida por pasos:
   - `TWOBLOCKS = BITMAPBLOCKSPERROW - NUMSTEPS_Y`
   - en cada step: 1 o 2 blits segun umbral (`TWOBLOCKSTEP`)
2. Politica de evitar picos de carga cada 16 px repartiendo trabajo intermedio.

Destino en engine:

- `engine/include/engine_external_scroll.h`
- `engine/src/external_scroll.c` (`engine_external_xyunlimited2_block_step_plan`)

Consumo actual:

- `tests/amiga-battery/ST01_scroll_x_external/src/main.c`

## Pendiente de extension

1. Extraer el split vertical de ST02 a helper reusable de engine.
2. Aplicar scheduler a tilebuffer retained (ST04) con mapa real de bloques.
3. Definir benchmark comparativo por frame budget entre:
   - path actual engine
   - path adapter externo

## Origen 2b: amiga-stuff yunlimited2 / cop_y2.asm

Fuentes:

- `C:/Users/dvdjg/Documents/programa/AI/amiga-stuff/scrolling_tricks/yunlimited2.c`
- `C:/Users/dvdjg/Documents/programa/AI/amiga-stuff/scrolling_tricks/cop_y2.asm`

Elementos importados:

1. Buffer circular vertical con altura extra oculta.
2. Recarga incremental de la franja que entra por arriba/abajo.
3. Split de copper cuando la ventana visible cruza el final del buffer.

Destino actual:

- `tests/amiga-battery/ST02_scroll_y_external/src/main.c`

Destino futuro en engine:

- helper reusable de split vertical sobre `engine_copper_list` / `engine_view`

## Origen 3: ACE tilebuffer (MPL-2.0)

Fuente:

- `C:/Users/dvdjg/Documents/programa/AI/Amiga-C++/ACE/src/ace/managers/viewport/tilebuffer.c`
- `C:/Users/dvdjg/Documents/programa/AI/Amiga-C++/ACE/include/ace/managers/viewport/tilebuffer.h`

Elementos importados:

1. `shiftFromPowerOfTwo()` para tamaño de tile -> shift.
2. Geometria de margenes de redraw en tiles:
   - longitudes en X/Y derivadas de viewport/tile shift y margenes scroll/draw.
3. Recomendacion de capacidad de cola en doble buffer:
   - estrategia "enqueue twice" para mantener coherencia entre back/front.

Destino en engine:

- `engine/include/engine_external_tilebuffer.h`
- `engine/src/external_tilebuffer.c`
- `engine/include/engine_scene_tilebuffer.h`
- `engine/src/scene_tilebuffer.c`

