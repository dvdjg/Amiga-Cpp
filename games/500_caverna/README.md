# 500 Caverna (vertical slice 2D, esqueleto V0/V1)

Primer consumidor de la fachada de juego para la slice «Caverna»: mapa de tiles fijo de **256×20** celdas, cámara horizontal, jugador móvil y HUD de host. El juego usa `App`/`Screen`/`input`/`present` y no ve `FramePlan`, planos, Copper ni memoria.

Estado actual: **esqueleto jugable V0/V1**. El fondo es un `FixedTileLayerMap<256>` con scroll XLimited y un lienzo FG para el jugador. Faltan el DPF 3+3 completo, colisión contra tiles, enemigos, audio y la prueba automática de latencia; el presupuesto está en `docs/guides/roadmap/ROADMAP_VERTICAL_SLICE_2D.md`.

## Controles

- Pad 0: izquierda/derecha mueven la cámara y el jugador; arriba/abajo mueven el jugador.

## Ejecutar

```bash
bash tools/build/build-demo.sh games/500_caverna --release
bash tools/run/run-demo.sh games/500_caverna --config A500_release
```

Medición: `2026-10-24`, A500_release, ventana de 12 s: **49,89?50,00 fps**, 1,000 campos/frame, `stable=true`.
