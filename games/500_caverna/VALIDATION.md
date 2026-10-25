# VALIDATION - 500 Caverna

> **Demo de capacidades: NO VERIFICADA (DT-046).** Los actores usan un rectángulo de prueba (0x03c0) y los enemigos actualizan posición cada 16 frames; no cumple las reglas de fluidez/atractivo. La validación de compositor y fps de abajo se refiere solo al mecanismo, no a la calidad visual final.

## Medición

```text
500_caverna/A500_release | emulado=50,04 fps | 141761 ciclos/frame | 0,99919 campos/frame | stable=true
500_caverna/A500_caverna_single1_release | emulado=49,92 fps | 142102 ciclos/frame | 1,00159 campos/frame | stable=true
```

Ventana de 8 s; measure-fps. La variante single se compila con EXTRA_DEFINES=-DCAVERNA_SINGLE=1 y desactiva DPF en init; conserva los ocho sprites y sus placements. La media es 49,89 fps (host) y las muestras individuales oscilan entre 49,63 y 50,32 fps por la cuantización de frames por ventana. La medición es en reposo, sin entrada sostenida; no se ha medido el coste de un salto o desplazamiento continuo.

El mapa es `FixedTileLayerMap<256>` y la cámara se conduce por la fachada. El display es **DPF 3+3** de 320×208; el jugador y los siete enemigos son sprites hardware (8 canales).

## Alcance verificado

- build y ejecución en WinUAE/A500;
- mapa fijo, scroll horizontal, jugador visible y cámara que sigue al jugador;
- colisión de la caja 16×16 del jugador contra los tiles sólidos del mapa (suelo y pilares);
- gravedad, salto con botón izquierdo del ratón, fuego del joystick o arriba; el jugador vuelve a aterrizar en el suelo;
- ocho actores hardware (jugador + siete enemigos) en los ocho canales, con patrulla y animación de DATA alterna;
- `SpriteDirector` actualiza posiciones sin reemitir y `XlimitedDualComposer` parchea POS/CTL; `SpriteBank` aportan DATA y slots;
- `SpriteDirector` mantiene el contrato de estado (L1-006);
- HUD de host con `CAVERNA DEMO 500`, versión, frame y fps;
- sin audio ni pitidos;
- ratón y joystick procesados como mensajes del mini-OS (`MouseMove`/`MouseButton`/`Joystick`), no por sondeo directo de `JOY0DAT`.

## Pendiente / NO VERIFICADO

- plataformas de tilemap más ricas y colisión del jugador contra todas las caras de un tile (hoy la resolución es de caja contra celdas sólidas, con el suelo y los pilares actuales);
- colisión completa con enemigos y reacción de juego (hoy se invierte la patrulla y se revierte el movimiento horizontal);
- latencia input→visible automatizada;
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

Con `--joy 1:up --joy-through-sequence --sequence-frames 6 --sequence-interval-ms 40` la secuencia muestra al jugador por encima del suelo y después aterrizando. La comprobación es manual/asistida por visión; la automatización de píxeles y de latencia sigue pendiente. La evaluación canónica de visión está en `src/VISION.md`.

La comprobación semántica específica de salto se ejecuta con:

```bash
node tools/analyze/vision-jump.mjs out/run/500_caverna/A500_release/sequence --require
```

Telemetria del compositor (HUD temporal): `FE=4` y `PH` crece en dual (40 y 56) y single (30); el parcheo POS/CTL se aplica. Se corrigio el contador `debug_patch_hits()` del compositor dual, que no se incrementaba.

La sonda actual responde `SALTO: NO` para las secuencias probadas: detecta desplazamiento horizontal, pero el VLM no confirma la subida/bajada vertical. Se deja como NO VERIFICADO hasta ajustar la inyección del salto o la captura temporal.
