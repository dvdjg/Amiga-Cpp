# Intencion de la demo 095

## Qué hace

Construye un cubo jugador y una puerta estatica en una escena 3D retenida. El jugador se mueve horizontalmente y cada frame se consulta un trigger OBB con filtro de capas mediante `trigger_sphere`. La puerta cambia de color cuando el jugador entra; el render usa siluetas convexas y `PolygonFillService` con Blitter, no el raster CPU.

## Qué pretende demostrar

Que un juego puede describir un trigger volumetrico con capa/mascara y consultar una esfera contra un OBB sin tocar registros del chipset. Tambien valida que la ruta de silueta por Blitter mantiene contornos limpios mientras el jugador se mueve y que la capa del jugador (2) solo interactua con la mascara de la puerta (1 -> 2).

## Elementos en pantalla

- Una puerta rectangular grande, cian cuando el jugador esta fuera y rosa cuando esta dentro.
- Un cubo amarillo que se desplaza horizontalmente y atraviesa la puerta.
- Fondo negro uniforme y HUD con el estado `IN/OUT`, la capa y el numero de objetos pintados.

## Evolución temporal

El cubo recorre la puerta de derecha a izquierda. Al entrar en el volumen, el trigger se activa y la puerta cambia de color; al salir, vuelve al color base. La secuencia de seis frames se valida con vision local y con `verify-3d-sequence --require-motion`.