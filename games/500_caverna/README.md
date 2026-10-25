# 500 Caverna (vertical slice 2D, V0/V1/V2/V3 + V5)

Primer consumidor de la fachada de juego para la slice «Caverna»: mapa de tiles fijo de **256×20** celdas, cámara horizontal con dead-zone, jugador con salto/gravedad/colisión, **ocho enemigos** con reacción (stomp/daño/vidas/score), HUD de host y audio. El juego usa `App`/`Screen`/`input`/scroll y no ve `FramePlan`, planos, Copper ni memoria.

## Estado actual

- **Arte y audio por manifiesto**: `src/assets.manifest.json` + `src/prebuild.sh` generan los blobs (`tools/assets/gen-caverna.mjs`) y `assets.manifest.hpp`. El juego registra paleta, mapas, filas y audio en `eng::Assets`; las cuatro hojas de sprites se copian desde los blobs al bloque Chip de la escena (`config.sprite_data_bytes = 4 × 128`).
- **Fondo** `FixedTileLayerMap<256>` con scroll XLimited y **DPF 3+3** (320×208). El suelo y las plataformas flotantes viven en PF2; el staging de las filas 14..19 queda vacío.
- **Actores**: jugador + ocho enemigos (nueve entidades de `SpriteDirector`). Patrullan, hacen stomp o daño, descuentan vidas y suman score; al quedarse sin vidas el nivel se reinicia.
- **Saltar**: arriba/fuego del joystick o botón izquierdo del ratón. Mantener pulsado vuelve a saltar al tocar suelo.
- **Cámara**: dead-zone `[96, 224]`; sigue al jugador solo al salir de la banda.
- **Audio**: módulo P61 `testmod.p61` y cuatro SFX del manifiesto (`jump`, `stomp`, `damage`, `land`).

## Pasada de sprites

Caverna registra una `SpriteDirectorPass` con `app.add_sprite_pass`: el `App` ejecuta el director antes del scroll y la escena recibe placements HW y el plan de BOB/CPU. La pasada **no es una capa visual**: el director decide HW/BOB/CPU y la prioridad frente a playfields es `SpriteHint::sprite_priority` por actor. `refresh_positions` actualiza X/Y, alto y DATA; el compositor parchea POS/CTL **por placement** (también los rearmes de un canal reutilizado por multiplexado vertical) y solo reemite si cambia la estructura. Los actores fuera de pantalla se culled.

El anillo del scroll usa `viewport_h + 2·tile` (240 px) para el staging del walk horizontal.

## Controles

- **Ratón**: el movimiento horizontal desplaza al jugador y la cámara; el botón izquierdo salta. Se procesa por mensajes `MouseMove`/`MouseButton` del mini-OS (`eng::os`).
- **Joystick**: conectar en el **puerto 2**; izquierda/derecha mueven al jugador; arriba o el botón de fuego saltan. Además de los mensajes del mini-OS, `update` lee el puerto 2 por hardware (`eng::amiga::poll_input`) como fallback.
- La velocidad horizontal es 2 px/frame.

## Ejecutar

```bash
bash tools/build/build-demo.sh games/500_caverna --release
bash tools/run/run-demo.sh games/500_caverna --config A500_release
```

Medición: `2026-10-25`, A500_release, ventana de 12 s: **49,27 fps** estático (143990 ciclos/frame) y **49,51 fps** con joystick derecho sostenido (143276 ciclos/frame). El detalle de validación y las limitaciones están en `VALIDATION.md`; la evaluación de visión en `src/VISION.md`.
