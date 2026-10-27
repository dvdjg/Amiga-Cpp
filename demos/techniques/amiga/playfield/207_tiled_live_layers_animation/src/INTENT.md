# INTENT - 207_tiled_live_layers_animation

## Qué hace

Extiende el bake de `Beginning Fields` con **celdas animadas vivas**: flores, agua y fogata se resuelven en runtime desde un banco planar de frames y máscaras 1bpp. La base cocida contiene `Ground` y las capas estáticas que quedan por encima; el animador parchea las celdas visibles cuando su grupo cambia de frame. La cámara queda fija para validar las tres animaciones sin confundirlas con el scroll.

## Qué pretende demostrar

- Que el cooker host conserva animaciones de capa (`Flowers`, `Water`) y de tile objects (`Campfire`) sin que el Amiga parsee TMX/TSX/PNG ni cuantice.
- Que un `TileAnimator` determinista (ticks de 50 Hz, frames y duraciones) puede parchear por Blitter sin recomponer la capa completa, repartiendo grupos cuando el `FramePlan` se llena.
- Que tres capas vivas pueden convivir en una cámara fija con una base que conserva `Road`, `Shadows` y `RockSlopes` cocidos, con el aviso explícito de posible solape.
- Que el frame-diff objetivo distingue las animaciones aunque el modelo de visión local no vea detalles de 16 px.

## Elementos en pantalla

- Mapa `Beginning Fields` completo, EHB 6 planos, 320×208 visibles (más 48 px de franja HUD negra), cámara fija en `Y=300`.
- Flores rojas/blancas y agua animadas por parche de tiles; fogata de 32×32 (4 celdas) animada; banderolas, edificios y terreno estáticos.
- HUD de host con `X`, `Y`, frame de animación (`A`) y número de grupos (`G`).

## Evolución temporal

1. **Init**: base cocida con `Ground` y capas estáticas, `pre_scroll` a `Y=300`, se compone la pila de tres capas vivas y se toman las tres animaciones en su primer frame.
2. **Frames**: la cámara no se desplaza; `patch_animations` pinta solo las celdas visibles de los grupos cuyo frame cambia (1/6 de grupos por tick), reparte grupos pequeños en round-robin por capa y, para la fogata, repinta primero el bloque base y después el frame enmascarado.
3. **Cierre**: bucle continuo. La deuda abierta es el coste: 24,25 fps con las tres capas activas; el frame ocupa 2,062 campos y queda en el borde del suelo de 25 fps.
