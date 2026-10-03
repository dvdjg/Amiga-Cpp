# HOST-243: coherencia de la selección de plaquetas del scroll

`eng/field/scroll_engine.hpp`: verifica que el **corkscrew XYLimited** elige las plaquetas
(bloques) de forma coherente sobre **mapas aleatorios** (mundo y wrap variados), a paso de **1 px**
y de **16 px**.

## Qué comprueba

1. **Columna completa**: en cada tile, el motor pinta **una** columna del mapa y sus filas del
   anillo forman un **rango contiguo y completo** (`bitmap_blocks_per_col` filas), sin huecos ni
   repeticiones. Se repite sobre 80 mapas aleatorios (ancho/alto y wrap variados).
2. **1 px == 16 px**: el `burst_right_px(16)` pinta **exactamente la misma secuencia de plaquetas**
   que 16 pasos de 1 px (misma elección; solo cambia el cálculo de la geometría).
3. **Mapa acotado**: el scroll se detiene en el tope sin saltar columnas.
4. **Límite del corkscrew**: el algoritmo asume tile de 16 px con
   `bitmap_blocks_per_col >= tile_w + 2`; con anillo 288 y tile 16 se cumple (18 ≥ 18); tile 32 con
   anillo 320 no (10 < 34), así que el corkscrew no soporta esos tiles (lo pinta/verifica el test).

## Nota

El "tileset" no influye en la geometría del blit (el índice de tile no cambia el destino); lo que
se ejercita es la **geometría del mapa/anillo**, que es donde vive la coherencia de la selección.
