# INTENT — 209 Beginning Fields 3bpp

## Qué hace

Recorre el mapa **Beginning Fields** a **3 planos (8 colores)** con un bitmap flat de 640×640 y una cámara Catmull-Rom en 16.8. El scroll solo mueve `BPLxPT`/`BPLCON1`; no hay blits por frame. El fuego y las flores se animan ciclando por Copper los índices de paleta 7 y 6, que el asset reserva para esas capas. Agua y sombras quedan cocidas en el bitmap.

## Qué pretende demostrar

- Que un mapa de rol con estructura y sombras cabe en 8 colores con dithering Floyd–Steinberg y se lee con claridad.
- Que un scroll de mundo grande a 3 planos puede costar cero blits por frame: solo copperlist y punteros.
- Que una cámara de spline en coma fija 16.8 se convierte a píxel en el compositor sin saltos: 6 px/frame constantes en la visita normal y 16 px/frame en turbo, con paradas periódicas.
- Que la animación de capas vivas puede resolverse por paleta (Copper) sin save-under ni parches de tiles.

## Elementos en pantalla

- Mapa completo Beginning Fields: casas, caminos, árboles, agua, sombras y vegetación; ventana de 320×256 sobre un mundo de 640×640.
- Cámara en una pasada zigzagueante por ocho paradas (Catmull-Rom cerrada).
- Fuego (índice 7) y flores (índice 6) ciclando color a 25 y 12,5 fps.
- HUD de host con nombre/número/versión/frames/fps y `X`, `Y`, segmento y `T`.

## Evolución temporal

1. **Init**: se carga el mundo planar de 153.600 B en el bitmap flat, se fija la paleta de 8 colores y se compone/toma el display.
2. **Frames**: la cámara avanza por la spline y actualiza ambos ejes; el compositor reemite punteros/fine scroll y la paleta 6/7 cambia por tick. No se ejecuta ningún blit de scroll.
3. **Cierre**: bucle continuo con paradas; ~50 fps estables (un campo/frame) tanto a 6 como a 16 px/frame.
