# 225 — SpriteDirector stress

## Qué hace

La demo ejercita la fachada por intención `SpriteDirector` sobre una escena PAL de 320×256 y 4 planos. Registra contenido simple y compuesto en `SceneAssets`, da de alta entidades con `SpriteDesc`/`SpriteHint` y compone cada frame con `emit`; después publica placements y paletas con `present`. El objetivo es que la escena mezcle casos que suelen probarse por separado: dos sprites contiguos que deben separarse por el gap de 1 línea, un par *attached* de 15 colores, una tira horizontal de dos tramos, un sprite compuesto de una parte, un BOB planar forzado y una banda de paleta con scroll vertical. El juego no escribe canales, `SPRx*` ni `BlitJob`.

## Qué pretende demostrar

Demostrar que la fachada nueva mantiene el contrato de hardware sin que la app conozca el chipset: el `SpriteAllocator` real decide canales y gap, `SpriteManager` arma/rearma la copperlist, `CompositeScene` y `SpriteScene` comparten el ledger y `SpriteLoadReport` mide el coste. Además, la demo usa el **HUD de depuración del host** (`app.debug().text`) para mostrar nombre, número, frame renderizado y fps, y no usa el viewport para ese HUD. La franja roja/amarilla inferior demuestra `BandPalette` con `top + scroll_y` y el evento de paleta de sprites.

## Elementos en pantalla

- Fondo azul oscuro (playfield 4 planos) y franja roja/amarilla de la banda de paleta en la parte baja.
- Dos sprites de silueta diagonal en la zona superior izquierda: el primero y el segundo están separados por el gap vertical; no deben compartir canal.
- Un sprite simple hacia la izquierda/centro, otro par *attached* de patrón azul punteado (dos canales), una tira de dos tramos verdes/azules y un compuesto que se mueve en la zona central.
- Un BOB rectangular gris oscuro en la zona superior derecha (hoja planar, no sprite).
- Texto de depuración en la esquina superior izquierda: `225 SPRITE DIRECTOR`, una línea `F<frame> <fps>FPS` y una línea `HW<n> BOB<n> BAND<n>`.

## Evolución temporal

Los sprites simples oscilan horizontalmente con una tabla de seno de 64 entradas y mantienen su Y; el compuesto también oscila en X/Y. La tira se desplaza como un bloque (sus dos tramos comparten fase), el BOB permanece fijo y la banda de paleta cambia `scroll_y` entre -8 y +7 líneas, de modo que la frontera rojo/amarillo sube y baja cada frame. El contador de frames y los fps del HUD se actualizan en el overlay del host; la animación debe ser continua, sin saltos de canal visibles ni parpadeo de los sprites. El caso de dos sprites contiguos es el contrato de gap: si el allocator los pusiera en el mismo canal en líneas consecutivas, el DMA no reencontraría la cabecera y se vería un corte/desaparición; el resultado correcto es que se repartan en canales distintos (o degraden) y se mantengan visibles.
