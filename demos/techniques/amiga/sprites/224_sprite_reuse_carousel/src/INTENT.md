# INTENT — 224_sprite_reuse_carousel

## Qué hace

La demo mantiene cinco copperlists preconstruidas con cinco bandas raster cada una. En cada banda se arman los ocho canales Sprite HW: los canales 0/1 y 2/3 sirven dos pares *attached* (7 colores opacos más transparencia) y los canales 4–7 sirven cuatro sprites libres (3 colores más transparencia). Cada canal aparece una vez por banda, de modo que se reutiliza cinco veces por frame y muestra un objeto distinto en cada banda. Por frame la CPU solo parchea `SPRxPOS`/`SPRxCTL` de los 40 placements; una vez por segundo publica otra copperlist con `COP1LC` (swap de la lista ya precocinada) y parchea después la nueva, que aún está inactiva. El resto lo ejecuta el DMA de sprites.

## Qué pretende demostrar

Que el hardware del A500 puede mover y reutilizar los ocho canales con transparencias y pares *attached* a 50 fps con intervención mínima de CPU, si la estructura de Copper es estática. La prueba compara además el coste con el camino de recomposición de `SpriteScene` (medido aparte) y fija las reglas de materialización que el planner debe garantizar sin que el programador toque canales. Cada objeto tiene su propia X animada por todo el ancho de la pantalla, de modo que ninguna banda queda en una columna fija, y cada banda oscila en vertical sin invadir la siguiente.

## Elementos en pantalla

- Fondo planar de bloques 32×16 con contraste alto, pintado una vez y sin coste por frame. Solo usa COLOR00..07/15 para no colisionar con los registros de los sprites.
- Dos lanes *attached* (canales 0/1 y 2/3): gemas multicolor con cinco siluetas (rombo, disco, flecha, cruz, escudo).
- Cuatro lanes libres (canales 4–7): formas de tres colores con cuatro siluetas distintas.
- La identidad visual se basa en color y forma, porque la paleta de sprites es compartida. El arte rota por las bandas una vez por segundo.
- Franja inferior con nombre y versión en los bitplanes. El contador de frames y los FPS se dibujan en el overlay de depuración del host (`F<frame> <fps>FPS`, limpiando el overlay antes de cada actualización); la telemetría completa de placements, canales, reusos y words parcheadas va por el periférico de depuración.

## Evolución temporal

Cada lane avanza su fase X con velocidad propia y su fase Y con un desfase por banda; el triángulo de 8 bits se convierte en X de 16 a 301 px y en una oscilación vertical de 0 a 15 líneas dentro de la banda. La separación entre bandas (52 líneas) y el alto del sprite (16) garantizan que `VSTOP` no alcanza el `WAIT` siguiente. Cada 50 frames (1 s a 50 Hz) el carrusel publica la siguiente copperlist: el arte de cada canal avanza una banda y el carrusel se cierra visualmente. Los puntos clave de captura son el arranque, un cruce de lanes, el instante de rotación y los extremos de la oscilación. La sincronización es por sondeo de `VPOSR` (`VblankWait::Polling`), porque bajo el runner AmigaDOS sigue vivo y reescribe `INTENA` (familia 212/DT-011).

## Criterio visual de éxito

En todos los frames clave deben verse las cinco bandas pobladas por los ocho canales, con contornos limpios, sin restos del objeto anterior, sin huecos en el fondo y sin cambios de color erráticos. El movimiento horizontal debe ser continuo y las rotaciones, deterministas. La cadencia objetivo es un field PAL por frame (≥49,5 fps), sin picos. La transparencia debe dejar ver el fondo en las siluetas. El perfil de visión y el frame-diff excluyen la franja del HUD.

## Configuraciones

A (3 planos), B (4), C (5) y D (6, EHB). El resultado se registra por perfil como PASS, FAIL o SIN VERIFICAR; ninguna media oculta un perfil sin medir. El camino estático no depende del número de planos, que es justo lo que la prueba debe confirmar en las cuatro configuraciones.
