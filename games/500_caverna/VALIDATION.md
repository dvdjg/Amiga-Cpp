# VALIDATION - 500 Caverna

## Medición

```text
500_caverna/A500_release | emulado=50,00 fps | 141869 ciclos/frame | 0,99996 campos/frame | stable=true
```

Ventana de 12 s; `measure-fps`. La media es 49,92 fps y las muestras individuales oscilan entre 49,27 y 50,58 fps por la cuantización de frames por ventana. La medición es en reposo, sin entrada sostenida; no se ha medido el coste de un salto o desplazamiento continuo.

El mapa es `FixedTileLayerMap<256>` y la cámara se conduce por la fachada. El display es **DPF 3+3** de 320×208; el jugador y los cuatro enemigos son sprites hardware (5 canales).

## Alcance verificado

- build y ejecución en WinUAE/A500;
- mapa fijo, scroll horizontal, jugador visible y cámara que sigue al jugador;
- colisión de la caja 16×16 del jugador contra los tiles sólidos del mapa (suelo y pilares);
- gravedad, salto con botón izquierdo del ratón, fuego del joystick o arriba; el jugador vuelve a aterrizar en el suelo;
- cuatro enemigos con patrulla y animación de DATA alterna, sin canales de sprite adicionales;
- HUD de host con `CAVERNA DEMO 500`, versión, frame y fps;
- sin audio ni pitidos;
- ratón y joystick procesados como mensajes del mini-OS (`MouseMove`/`MouseButton`/`Joystick`), no por sondeo directo de `JOY0DAT`.

## Pendiente / NO VERIFICADO

- plataformas de tilemap más ricas y colisión del jugador contra todas las caras de un tile (hoy la resolución es de caja contra celdas sólidas, con el suelo y los pilares actuales);
- colisión completa con enemigos y reacción de juego (hoy se invierte la patrulla y se revierte el movimiento horizontal);
- ocho actores y animación de dos frames por actor;
- latencia input→visible automatizada;
- manifiesto de assets y presupuesto completo de la slice;
- audio: **diferido por decisión del usuario**, sin tono provisional.

## Pruebas de entrada

Salto con joystick del puerto 2 (mantener arriba o fuego):

```bash
bash tools/run/run-demo.sh games/500_caverna --config A500_release --wait-ms 800 --joy 1:up --joy-hold-ms 40 --sequence-frames 3 --sequence-interval-ms 80
```

Salto con ratón (botón izquierdo) y desplazamiento horizontal:

```bash
bash tools/run/run-demo.sh games/500_caverna --config A500_release --wait-ms 800 --mouse-from 120,100 --mouse-to 220,100 --sequence-frames 3 --sequence-interval-ms 200
```

Con `--joy 1:up --joy-hold-ms 40` la secuencia muestra al jugador por encima del suelo y después aterrizando. La comprobación es manual/asistida por visión; la automatización de píxeles y de latencia sigue pendiente. La evaluación canónica de visión está en `src/VISION.md`.
