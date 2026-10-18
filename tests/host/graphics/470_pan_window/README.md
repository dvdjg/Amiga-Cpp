# HOST-470: politica de paneo de ventana (`eng::graphics/pan_window.hpp`)

Test host de la decision **Full/Fake** para los fotogramas intermedios por paneo (tecnica de `docs/reference/amiga/techniques/camera-pan-fake-frames.md`). El header es puro (sin hardware): el llamador acumula el paneo de camara en px de ventana y la politica decide cuando rendirizar la pasada completa y cuando presentar el frame anterior desplazado.

## Que comprueba

1. **Basico**: el primer paso es Full (ancla la base); los fakes llevan el offset exacto (positivo, negativo y por ejes).
2. **Margen**: un delta mayor que `margin_x`/`margin_y` fuerza Full y re-ancla la base.
3. **Racha**: `max_consecutive_fakes` limita los fakes seguidos; el Full reinicia la racha.
4. **Forzado**: `pan_window_force_full` (cambio de escena) y el invariante `pan_window_offset_fits` en un barrido de deltas.
5. **Convencion de scroll** (`playfield_scroll.hpp`): la descomposicion ventana S -> puntero coarse + fino (`fine_scroll_coarse`/`fine_delay`) para todo S en 1..400 y los casos concretos que usa la demo 080 (S=16 centro, S=17 fake +1, S=31 margen).

## Salida de referencia

```
Politica de paneo de ventana (pan_window):
OK: pan window (full/fake, margenes, racha, forzado y convencion de scroll)
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/graphics/470_pan_window
```
