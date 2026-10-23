# HOST-486 — HUD de depuración del host (`HostHud`)

Respalda `eng/debug/host_hud.hpp`: el HUD que todas las demos/juegos escriben en el overlay de depuración del host (`app.debug().text`) sin consumir bitplanes ni DMA.

## Contrato cubierto

- Nombre + número de demo + versión `vM.m.B` en la primera línea.
- Frame renderizado y fps ×10 en la segunda; la ventana de fps se cierra a ~1 s.
- Línea opcional de telemetría propia (carga, piezas, blits, cobre…).
- `clear=false` respeta otra telemetría ya presente en el overlay.

## Casos

- `test_identity_and_fps`: nombre/versión, armado de la ventana, 20 frames en 1 s (`20.0FPS`) y `fps10() == 200`.
- `test_stats_and_clear`: línea de stats y limpieza opcional.

El overlay es un doble de prueba; no usa WinUAE, hardware ni `g_eng_run_status`.

```bash
bash tools/run-host-tests.sh tests/host/debug/486_host_hud
```
