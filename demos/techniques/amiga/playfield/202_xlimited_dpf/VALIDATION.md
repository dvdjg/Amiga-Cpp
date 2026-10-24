# Validación — Demo 202 (`xlimited_dpf`)

Intención: dual playfield 3+3 con scroll XLimited y mapa real; debe sostener 50 Hz PAL.

El objetivo «50 fps PAL» es **una iteración por campo PAL nominal** (~49,9–50,0 en `measure-fps` por la ventana y la cuantización de campos), no 50,000 exactos. Se exige cadencia estable y ausencia de frames repetidos, no un literal.

## Medición entregada

```text
202_xlimited_dpf/A500_release | emulado=49,92–50,00 fps | 141870–142102 ciclos/frame | 0,99996–1,00159 campos/frame
202_xlimited_dpf/A500_debug   | emulado=49,9 fps (ventana corta) | HUD a 1/8 de refresco
```

La captura final muestra el mapa, las plaquettes y la franja inferior completos; el HUD de una línea aparece en el margen seguro.

Ventana larga de 12 s (`A500_release`, árbol actual): **49,92–50,00 fps**, 141870–142102 ciclos/frame y 0,99996–1,00159 campos/frame. La cadencia queda dentro del ruido de la ventana de 1 s (49,3–50,6 fps), no en la meseta de 46–47 fps anterior. El 3+3 se mantiene a 320×208; la última pasada cerró en 50,00 fps y 0,99996 campos/frame.

## Sonda de carga de juego

La compilación opcional `EXTRA_DEFINES="-DK_202_GAME_LOAD=1"` añade 24 actores de lógica con movimiento, rebote y comprobación de pares, más un sprite DMA transparente de 16 líneas. No añade audio todavía. La sonda mantiene el DPF 3+3 y mide **49,79 fps** en ventana de 12 s (142471 ciclos/frame, 1,00419 campos/frame, `stable=true`); sirve para comprobar que el margen de CPU no se agota con una carga de juego mínima. El perfil entregado sigue siendo `A500_release` sin la macro.

## Diagnóstico y cambios

El coste estaba en dos consultas del camino caliente que pagaban módulos de 32 bits (`__modsi3`) aunque el dato estuviera dentro de rango. En `TileLayerMap::tile_at()` el mapa toroidal de 40×40 consultaba `wrap_coordinate` para cada celda; en `map_ring_scroll()` el offset vertical normalizaba `videoposy` con dos `%` antes de aplicar el `fast_div`. La instrumentación por secciones midió ~7,5k ciclos en la resolución de celda de un solo bloque y ~7,8k por `hardware_view`; tras los fast paths bajan a ~0,7k y ~5,0k respectivamente. La suma de los dos `update_scroll` pasó de ~52k a ~15k ciclos, y `compose` a ~17k (incluye las dos vistas y el `patch`).

Correcciones:

- `TileLayerMap::tile_at()` resuelve directamente las coordenadas dentro del mapa cuando `wrap_x == 0 || wrap_x == width` y `wrap_y == 0 || wrap_y == height`; conserva el camino general para wrap con periodo distinto del tamaño.
- `map_ring_scroll()` calcula `(videoposy + tile_h) mod display_h` directamente, sin normalizar `videoposy` dos veces; mantiene el camino negativo (un solo módulo) y el resultado validado por `HOST-063`.
- Test de equivalencia de `TileLayerMap` en `tests/host/field/025_tile_source`: wrap con periodo igual y distinto del ancho, y coordenadas negativas.
- Test existente de `map_ring_scroll` (`tests/host/field/063_ring_mapper`) cubre el camino compile-time, runtime y valores negativos.
- La demo entregada usa `kShareY=true` y `max_step=1`; se documenta la reducción frente a la variante de Y independiente.
- HUD de host a 1/8 de refresco.
- La instrumentación confirmó que el compositor dual usa `patch()` en el camino caliente y no reemite la copperlist cada frame. `ScrollFast1` no se usa porque su prefill avanza por tiles de 16 px; la demo conserva el paso de 1 px y el camino de ráfaga existente (`burst_right_px`).

## Deuda

La variante de **Y independiente** (`kShareY=false`) sigue sin caber en 50 Hz con el DPF 3+3 completo; requeriría un pre-render con lookahead o reducir la escena. No se declara válida como variante de 50 Hz.
