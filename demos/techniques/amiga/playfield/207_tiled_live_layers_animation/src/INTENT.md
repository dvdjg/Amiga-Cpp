# INTENT - 207_tiled_live_layers_animation

## Qué hace

Extiende el bake de `Beginning Fields` con **celdas animadas vivas**: flores, agua y fogata se resuelven en runtime desde un banco planar de frames y máscaras 1bpp. La base cocida contiene `Ground` y las capas estáticas que quedan por encima; el animador parchea las celdas visibles cuando su grupo cambia de frame. La cámara recorre el mapa completo en una visita con paradas de 3 s para observar las tres animaciones; el scroll no compite con el parcheo de capas vivas.

## Qué pretende demostrar

- Que el cooker host conserva animaciones de capa (`Flowers`, `Water`) y de tile objects (`Campfire`) sin que el Amiga parsee TMX/TSX/PNG ni cuantice.
- Que un `TileAnimator` determinista (ticks de 50 Hz, frames y duraciones) puede parchear por Blitter sin recomponer la capa completa, repartiendo grupos cuando el `FramePlan` se llena.
- Que tres capas vivas pueden convivir con una base que conserva `Road`, `Shadows` y `RockSlopes` cocidos, con el aviso explícito de posible solape, mientras la cámara recorre el mapa.
- Que el frame-diff objetivo distingue las animaciones aunque el modelo de visión local no vea detalles de 16 px.
- Que un objeto estático por encima de una celda viva (la banca junto a la fogata) no congela la animación: el cooker emite una capa de cobertura con sus píxeles y el runtime la reestampa después del parche.
- Que una silueta constante (el agua) se repinta con un solo cookie-cut, sin save-under ni `restore`, porque el propio cookie-cut conserva el fondo de los píxeles transparentes.

## Elementos en pantalla

- Mapa `Beginning Fields` completo, EHB 6 planos, 320×208 visibles (más 48 px de franja HUD negra), cámara en una visita que empieza centrada en la fogata, pasa por agua y flores y recorre las cuatro esquinas y el centro del mapa.
- Flores rojas/blancas y agua animadas por parche de tiles; fogata de 32×32 fusionada automáticamente en un sprite; banderolas, edificios y terreno estáticos.
- HUD de host con nombre/número/versión/frames/fps y, como diagnóstico, `X`, `Y`, frame de animación (`A`) y número de grupos (`G`). Las builds `DEMO_BLIT_STATS`/`DEMO_PROF` añaden jobs, celdas por capa y estado del cache.

## Evolución temporal

1. **Init**: base cocida con `Ground` y capas estáticas, se construye la lista de paradas desde las celdas vivas (fogata, agua, flores) y las esquinas, `pre_scroll` a la fogata, se captura el fondo real de cada celda visible con máscara variable en `MapCellSaveCache` y se compone la pila de capas vivas. Las celdas de agua (máscara fija) no capturan fondo; las celdas con cobertura se estampan al final para dejar el objeto por encima.
2. **Frames**: la cámara avanza 2 px por eje y frame; durante el desplazamiento solo se parchea la fogata (cada 5 frames) y las flores/agua se quedan en su último frame. En cada parada de 3 s, `patch_animations` usa la tabla de visibilidad cacheada, restaura el fondo de las celdas de máscara variable cuyo frame cambia, pinta el frame nuevo y reestampa la cobertura de la celda si la tiene; el overlay va en la columna de mundo (`wx`) y la visibilidad se decide con `sx`/`sy`. La capa superior (fogata) se procesa primero y el cooker la agrupa en un único grupo de 32×32; agua y flores se reparten con el cursor sobre la lista de grupos cambiados. La fusión es automática para clusters rectangulares de celdas contiguas con la misma cadencia y gids únicos. `set_tick` solo visita los grupos cuyo cambio vence en el tick (anillo de vencimientos). Si el frame supera ~1,25 campos, la política de presupuesto congela el agua (capa ambiente) y mantiene fogata y flores; el agua se reanima cuando el margen se recupera. En la parada de la fogata esto evita que el agua consuma el presupuesto del foco visual.
3. **Cierre**: bucle continuo de visita. La visita mide 39,90 fps de media (mínimo 35,4) con la fogata animándose en scroll; la fogata, el peor caso, 44,09. El 50 Hz sigue abierto; el flicker de buffer único se aborda con el scheduler de BOBs por deadlines de haz.
