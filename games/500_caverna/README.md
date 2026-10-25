# 500 Caverna (vertical slice 2D, V0/V1/V2/V3 parciales)

Primer consumidor de la fachada de juego para la slice «Caverna»: mapa de tiles fijo de **256×20** celdas, cámara horizontal, jugador con salto/gravedad, **ocho enemigos** repartidos por el nivel y HUD de host. El juego usa `App`/`Screen`/`input`/scroll y no ve `FramePlan`, planos, Copper ni memoria.

Estado actual: **esqueleto jugable V0/V1/V2/V3**. El fondo es un `FixedTileLayerMap<256>` con scroll XLimited y **DPF 3+3** (320×208); el jugador y los ocho enemigos son actores de `SpriteDirector` (nueve actores). El arte procedural (cueva, terreno y cuatro tipos de actor con dos poses) vive en `src/caverna_art.cpp`; `main.cpp` solo contiene la lógica de juego. El jugador colisiona con el tilemap y salta; los enemigos patrullan y animan. El HUD muestra `CAVERNA DEMO 500`. No hay audio ni pitidos. Siguen pendientes las plataformas de tilemap más ricas, el manifiesto de assets, la reacción completa de enemigos y la prueba automática de latencia.

## Pasada de sprites

Caverna registra una `SpriteDirectorPass` con `app.add_sprite_pass`: el `App` ejecuta el director antes del scroll y la escena recibe placements HW y el plan de BOB/CPU. La pasada **no es una capa visual**: el director decide HW/BOB/CPU y la prioridad frente a playfields es `SpriteHint::sprite_priority` por actor. `refresh_positions` actualiza X/Y, alto y DATA animada; el compositor parchea POS/CTL y solo reemite si cambia la estructura. Los actores fuera de pantalla se culled.

El anillo del corcóscru usa `viewport_h + 2·tile` (240 px) para el staging del walk horizontal. Las filas 14..19 del mapa PF2 son staging no visible y se dejan vacías; con contenido ahí, el scroll las revelaba como un bloque de terreno en la parte alta.

## Controles

- **Ratón**: el movimiento horizontal desplaza al jugador y la cámara; el botón izquierdo salta. Se procesa por mensajes `MouseMove`/`MouseButton` del mini-OS (`eng::os`), no por sondeo directo de `JOY0DAT`.
- **Joystick**: conectar en el **puerto 2**; izquierda/derecha mueven al jugador; arriba o el botón de fuego saltan. Los mensajes `Joystick` también llegan por `eng::os`.
- La velocidad horizontal es 1 px/frame para sostener 50 fps con DPF 3+3 y nueve actores.
- El programa no reproduce audio.

## Ejecutar

```bash
bash tools/build/build-demo.sh games/500_caverna --release
bash tools/run/run-demo.sh games/500_caverna --config A500_release
```

Medición: `2026-10-25`, A500_release, ventana de 20 s: **49,92 fps** estático (142102 ciclos/frame) y **49,82 fps** con la cámara en movimiento a 1 px/frame (142384 ciclos/frame), `stable=true`. A 2 px/frame la media baja a 40,80 fps (1/2 campos, inestable); por eso el juego usa 1 px/frame. La evaluación de visión y el band-diff están en `src/VISION.md` y `VALIDATION.md`.
