# 500 Caverna (vertical slice 2D, esqueleto V0/V1)

Primer consumidor de la fachada de juego para la slice «Caverna»: mapa de tiles fijo de **256×20** celdas, cámara horizontal, jugador móvil, cuatro enemigos como sprites hardware y HUD de host. El juego usa `App`/`Screen`/`input`/`present` y no ve `FramePlan`, planos, Copper ni memoria.

Estado actual: **esqueleto jugable V0/V1**. El fondo es un `FixedTileLayerMap<256>` con scroll XLimited y **DPF 3+3** (320×208); el jugador y los cuatro enemigos son sprites hardware. El HUD muestra `CAVERNA DEMO 500`. No hay audio ni pitidos. Siguen pendientes la colisión contra tiles, la música, el manifiesto de assets y la prueba automática de latencia.

## Controles

- **Ratón**: el movimiento horizontal desplaza al jugador y la cámara; se procesa por mensajes `MouseMove` del mini-OS (`eng::os`), no por sondeo directo de `JOY0DAT`.
- **Joystick**: conectar en el **puerto 2**; izquierda/derecha mueven cámara y jugador; arriba/abajo mueven al jugador. Los mensajes `Joystick` también llegan por `eng::os`.
- El programa no reproduce audio.

## Ejecutar

```bash
bash tools/build/build-demo.sh games/500_caverna --release
bash tools/run/run-demo.sh games/500_caverna --config A500_release
```

Medición: `2026-10-24`, A500_release, ventana de 12 s: **49,94 fps** emulados (media), 142036 ciclos/frame, 1,00113 campos/frame, `stable=true`.
