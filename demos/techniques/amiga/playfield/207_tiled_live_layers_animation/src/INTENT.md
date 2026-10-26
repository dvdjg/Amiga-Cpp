# INTENT — 207_tiled_live_layers_animation

## Qué hace

Extiende el bake de `Beginning Fields` con **celdas animadas vivas**: flores, agua y fogata se resuelven en runtime desde un banco planar de frames y máscaras 1bpp. El fondo base mantiene el primer frame de Tiled; el animador parchea las celdas visibles cuando su grupo cambia de frame.

## Qué pretende demostrar

- Que el cooker host conserva animaciones de capa (`Water`, `Flowers`) y de tile objects (`Campfire`) sin que el Amiga parsee TMX/TSX/PNG ni cuantice.
- Que un `TileAnimator` determinista (ticks de 50 Hz, frames y duraciones) puede parchear por Blitter sin recomponer la capa completa.
- Que un presupuesto por fases mantiene 50 fps con 6 planos EHB y 227 celdas animadas, en un recorrido Y-only.
- Que los elementos estáticos (banderolas, edificios, terreno) siguen viniendo del bake base sin cambios.

## Elementos en pantalla

- Mapa `Beginning Fields` completo, EHB 6 planos, 320×208 visibles (más 48 px de franja HUD negra), cámara Y-only sobre un mundo toroidal.
- Flores rojas/blancas y agua animadas por parche de tiles; fogata de 32×32 (4 celdas) animada; banderolas estáticas.
- HUD de host con `X`, `Y`, frame de animación (`A`) y número de grupos (`G`).

## Evolución temporal

1. **Init**: base cocida con el primer frame, `pre_scroll` de 300 px, se activa el animador y se compone el primer frame.
2. **Frames**: la cámara avanza 1 px en Y; el scroll pinta la fila entrante. Cuando un grupo de animación cambia, `patch_animations` pinta solo sus celdas visibles (1/6 de los grupos por frame).
3. **Cierre**: bucle continuo; rebote al llegar al borde del toroide.
