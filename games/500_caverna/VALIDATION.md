# VALIDATION - 500 Caverna

## Medición

```text
500_caverna/A500_release (estático)        | emulado=49,92 fps | 142102 ciclos/frame | 1,002 campos/frame | stable=true
500_caverna/A500_release (1 px/frame)      | emulado=49,82 fps | 142384 ciclos/frame | 1,004 campos/frame | stable=true
500_caverna/A500_release (2 px/frame)      | emulado=40,80 fps | 173868 ciclos/frame | 1,225 campos/frame | inestable
```

Ventana de 20 s; `measure-fps`. La fila de 1 px/frame se midió con una instrumentación de auto-scroll (`EXTRA_DEFINES=-DCAVERNA_AUTOSCROLL=1`, retirada del binario final): usa exactamente la misma ruta de scroll que la entrada real. La fila de 2 px/frame es el A/B que fija la velocidad de juego: el frame alterna 1 y 2 campos, por eso el jugador avanza 1 px/frame. El coste del sondeo de entrada real no se midió por separado (**SIN VERIFICAR**).

El mapa es `FixedTileLayerMap<256>` y la cámara se conduce por la fachada. El display es **DPF 3+3** de 320×208; actores: jugador + ocho enemigos (nueve entidades de `SpriteDirector`). El anillo usa `display_height=240` (`viewport_h + 2·tile`) para el staging del walk horizontal.

## Alcance verificado

- build y ejecución en WinUAE/A500;
- mapa fijo, scroll horizontal, jugador visible y cámara que sigue al jugador;
- colisión de la caja 16×16 del jugador contra los tiles sólidos del mapa (suelo, colinas y pilares);
- gravedad y aterrizaje; el salto por joystick/ratón existe en el código, pero su confirmación visual automática sigue pendiente (ver abajo);
- nueve actores (jugador + ocho enemigos) con arte legible, tipos y poses diferenciadas, patrulla cada frame y animación de arte cada 8 frames; el director reparte canales y culled los actores fuera del clip;
- `SpriteDirectorPass` conduce el director desde el `App` antes del scroll; `refresh_positions` actualiza X/Y, alto y DATA animada y el compositor parchea POS/CTL; `SpriteBank` ya no se usa en Caverna;
- fix de scroll: el walk horizontal lee hasta la fila de staging 17. Con filas 14..19 de PF2 vacías (`caverna_art.cpp`) y anillo de 240 px, no aparece el bloque de terreno que se veía en la parte alta al desplazar la cámara; verificado con captura paso a paso antes/después;
- HUD de host con `CAVERNA DEMO 500`, versión, frame y fps;
- sin audio ni pitidos;
- ratón y joystick procesados como mensajes del mini-OS (`MouseMove`/`MouseButton`/`Joystick`), no por sondeo directo de `JOY0DAT`.

## Visión y movimiento

`src/VISION.md` es la evaluación canónica (6 frames, `vision-final.mjs`). El modelo describe la cueva, el terreno, el jugador y los enemigos, observa el desplazamiento del objeto rosa y no reporta halos, recortes ni bandas rectangulares. El band-diff de `check-elements` entre el primer y el último frame cambia solo en las bandas de los enemigos (y=96..127 y 160..207) y deja estáticas las bandas de fondo y terreno, lo que descarta el bloque de staging.

## Pendiente / NO VERIFICADO

- plataformas de tilemap más ricas y colisión del jugador contra todas las caras de un tile (hoy es caja contra celdas sólidas);
- reacción completa con enemigos (hoy se invierte la patrulla y se revierte el movimiento horizontal);
- latencia input→visible automatizada y confirmación automática del salto: `vision-jump.mjs` responde `SALTO: NO` en las secuencias probadas;
- coste medido con entrada real sostenida (la medición de movimiento usa auto-scroll; el sondeo de entrada no se incluye);
- manifiesto de assets y presupuesto completo de la slice;
- audio: **diferido por decisión del usuario**, sin tono provisional.

## Pruebas de entrada

Salto con joystick del puerto 2 (mantener arriba o fuego):

```bash
bash tools/run/run-demo.sh games/500_caverna --config A500_release --wait-ms 800 --joy 1:up --joy-through-sequence --sequence-frames 6 --sequence-interval-ms 40
```

Salto con ratón (botón izquierdo) y desplazamiento horizontal:

```bash
bash tools/run/run-demo.sh games/500_caverna --config A500_release --wait-ms 800 --mouse-from 120,100 --mouse-to 220,100 --sequence-frames 3 --sequence-interval-ms 200
```

La comprobación semántica específica de salto se ejecuta con:

```bash
node tools/analyze/vision-jump.mjs out/run/500_caverna/A500_release/sequence --require
```

La sonda actual responde `SALTO: NO`: detecta desplazamiento horizontal, pero el VLM no confirma la subida/bajada vertical. Se deja como NO VERIFICADO hasta ajustar la inyección del salto o la captura temporal.
