# INTENT — 087_blit3d_triangles

## Qué hace

Primer eslabón de la pila de tests de la **librería 3D acelerada por Blitter** (`eng::graphics::render3d`, roadmap `ROADMAP_3D_BLITTER.md`): siete triángulos sólidos con colores distintos se mueven por un área de juego de 320×232, rotan en 2D, se solapan (painter por profundidad) y rebotan cruzando las cuatro esquinas (los clip de cámara y 2D recortan las caras que salen). Muestra el pipeline completo del paquete 1 de `3D_GAME_ARCHITECTURE.md` sobre el relleno por cara con Blitter ya existente.

## Qué pretende demostrar

- El **clip de cámara** (near + cuatro planos laterales) antes de dividir por `z` mantiene la proyección acotada al viewport y elimina los vértices fuera del bitmap que corrompen los contornos del Blitter.
- El **culling por área** y el **painter** entre sólidos funcionan con triángulos en movimiento que se solapan.
- El relleno por cara del backend (máscara 1 bpp + spans CPU + cookie-cut por plano) produce **caras sólidas limpias** sin rayas por filas de cruce impar.
- El coste real del camino por cara se mide y se atribuye (perfilador por secciones del engine).

## Elementos en pantalla

- **Área de juego** (filas 0..231): siete triángulos de 24..36 px de radio, colores 1..7 de la paleta, con giro propio y velocidades de rebote distintas; cada uno a una profundidad fija (900..4200) para que el solape sea determinista.
- **Franja inferior** (filas 232..255): nombre de la demo, versión con build y contador de frames `F<num>`.
- Sin sprites ni audio: el foco es el raster de polígonos por Blitter y el pipeline 3D.

## Evolución temporal

- Los triángulos avanzan 2-3 px/frame y rebotan fuera del área de juego (±70 px), de modo que entran y salen por las esquinas; el clip de cámara y el clip 2D de `Surface` recortan las caras parcialmente fuera.
- Cada triángulo gira a 3-7 vueltas/4096 por frame (sin/cos q12 con tabla); los colores no cambian.
- El contador de frames avanza con el latido de VBlank (espera activa, `set_vblank_sleep(false)`).
- **Presupuesto**: la demo está **ABIERTA** por rendimiento: 7,98 fps (6,27 campos/frame) frente al suelo de 25 fps de los efectos de relleno poligonal. El hotspot está localizado (tiempo de Blitter de las copias por plano del relleno por cara); la campaña de optimización es la fase P5 del roadmap. Detalle en `VALIDATION.md`.
