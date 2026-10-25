# 500 Caverna (vertical slice 2D, V0/V1/V2/V3 parciales)

Primer consumidor de la fachada de juego para la slice «Caverna»: mapa de tiles fijo de **256×20** celdas, cámara horizontal, jugador con salto/gravedad, siete enemigos patrullando como sprites hardware y HUD de host. El juego usa `App`/`Screen`/`input`/`present` y no ve `FramePlan`, planos, Copper ni memoria.

Estado actual: **esqueleto jugable V0/V1/V2/V3**. El fondo es un `FixedTileLayerMap<256>` con scroll XLimited y **DPF 3+3** (320×208); el jugador y los siete enemigos son sprites hardware (8 canales). El jugador colisiona con el tilemap y salta; los enemigos patrullan y animan. El HUD muestra `CAVERNA DEMO 500`. No hay audio ni pitidos. Siguen pendientes las plataformas de tilemap más ricas, el manifiesto de assets y la prueba automática de latencia.

## Deuda de sprites

Caverna usa ya `SpriteDirector` con ocho actores y canales fijos: `refresh_simple_positions` recalcula solo X/Y y `XlimitedDualComposer` parchea POS/CTL en la copperlist. `SpriteBank`/`SpriteGameView` siguen aportando DATA y el banco de slots; `SpriteDirector` mantiene el contrato de estado. La ruta lenta por `emit` completo queda para cambios estructurales (alta/baja o cambio de DATA).

## Controles

- **Ratón**: el movimiento horizontal desplaza al jugador y la cámara; el botón izquierdo salta. Se procesa por mensajes `MouseMove`/`MouseButton` del mini-OS (`eng::os`), no por sondeo directo de `JOY0DAT`.
- **Joystick**: conectar en el **puerto 2**; izquierda/derecha mueven al jugador; arriba o el botón de fuego saltan. Los mensajes `Joystick` también llegan por `eng::os`.
- El programa no reproduce audio.

## Ejecutar

```bash
bash tools/build/build-demo.sh games/500_caverna --release
bash tools/run/run-demo.sh games/500_caverna --config A500_release
```

Medición: `2026-10-25`, A500_release, ventana de 20 s: **49,92 fps** emulados (49,86 host), 142102 ciclos/frame, 1,00159 campos/frame, `stable=true`.
