# VALIDATION - 500 Caverna

## Medición

```text
500_caverna/A500_release | emulado=49,89–50,00 fps | 141869 ciclos/frame | 0,99996 campos/frame | stable=true
```

Ventana de 12 s; `measure-fps`. El mapa es `FixedTileLayerMap<256>` y la cámara se conduce por la fachada.

## Alcance verificado

- build y run en WinUAE/A500;
- mapa fijo, scroll horizontal y jugador visible;
- HUD de host con nombre, versión, frame y fps.

## Pendiente / NO VERIFICADO

- DPF 3+3 completo (ahora BG de 3 planos + lienzo FG);
- colisión contra tiles, enemigos, audio y SFX;
- latencia input→visible medida con inyección de pad;
- presupuesto completo de la slice (55k ciclos activos, 160 KiB Chip).


## Latencia input→visible

Prueba reproducible con inyección de joystick del runner:

```bash
bash tools/run/run-demo.sh games/500_caverna --config A500_release   --wait-ms 800 --joy 0:right --joy-hold-ms 200   --sequence-frames 3 --sequence-interval-ms 200
```

El jugador debe desplazarse en el primer frame posterior a la inyección (paso 2 px) y la cámara debe seguirlo sin dejar un frame intermedio con el jugador en la posición anterior. La comprobación visual se hace sobre la secuencia (`out/run/500_caverna/A500_release/sequence/`). Queda pendiente automatizar la comparación de píxeles; hoy es una prueba manual/vision.
