# VALIDATION - 500 Caverna

## Medición

```text
500_caverna/A500_release | emulado=49,94 fps | 142036 ciclos/frame | 1,00113 campos/frame | stable=true
```

Ventana de 12 s; `measure-fps`. La media es 49,94 fps y las muestras individuales oscilan entre 49,27 y 50,58 fps por la cuantización de frames por ventana. La medición es en reposo, sin entrada sostenida; no se ha medido el coste de un desplazamiento continuo.

El mapa es `FixedTileLayerMap<256>` y la cámara se conduce por la fachada. El display es **DPF 3+3** de 320×208; el jugador y los cuatro enemigos son sprites hardware (5 canales).

## Alcance verificado

- build y ejecución en WinUAE/A500;
- mapa fijo, scroll horizontal, jugador y cuatro enemigos visibles;
- HUD de host con `CAVERNA DEMO 500`, versión, frame y fps; la captura de visión lee el nombre y el número;
- sin audio ni pitidos;
- ratón y joystick procesados como mensajes del mini-OS (`MouseMove`/`Joystick`), no por sondeo directo de `JOY0DAT`;
- prueba manual de movimiento con ratón y con joystick del puerto 2.

## Pendiente / NO VERIFICADO

- colisión contra tiles y comportamiento completo de los enemigos (hoy son estáticos y solo hay una colisión de caja simple);
- música de tracker (o silencio definitivo) y SFX;
- latencia input→visible automatizada;
- presupuesto completo de la slice (55k ciclos activos, 160 KiB Chip).

## Latencia input→visible

Prueba reproducible con el joystick del **puerto 2** (el mensaje `Joystick` usa ese puerto; `1:right` del runner):

```bash
bash tools/run/run-demo.sh games/500_caverna --config A500_release --wait-ms 800 --joy 1:right --joy-hold-ms 200 --sequence-frames 3 --sequence-interval-ms 200
```

Prueba con ratón (movimiento relativo por mensajes `MouseMove`):

```bash
bash tools/run/run-demo.sh games/500_caverna --config A500_release --wait-ms 800 --mouse-from 120,100 --mouse-to 220,100 --sequence-frames 3 --sequence-interval-ms 200
```

El jugador debe desplazarse en el primer frame posterior a la inyección y la cámara debe seguirlo sin dejar un frame intermedio con el jugador en la posición anterior. La comprobación visual se hace sobre la secuencia (`out/run/500_caverna/A500_release/sequence/`). La comparación de píxeles y la medida de latencia quedan pendientes de automatización.

La evaluación de visión está en `src/VISION.md`; la secuencia generada sin entrada sostenida muestra la escena estática, no el movimiento.
