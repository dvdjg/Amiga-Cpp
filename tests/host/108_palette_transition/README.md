# HOST-108 — transición de paleta (efecto)

Test host de `eng/graphics/effects/palette_transition.hpp`: valida la lógica pura del
efecto `PaletteTransitionEffect` (estado temporal y parche de paleta) sin hardware ni
emulador. El efecto es gráfico, pero su cálculo es puro, así que se prueba aquí de forma
determinista; su integración con el driver de copper se respalda con la demo 040.

## Qué comprueba

1. **Una sola pasada** (`ping_pong = false`): `num/den` sube `0 → den` y, pasado el final,
   **no envuelve**: se queda en `den` (destino), y la paleta runtime queda en `to`.
2. **Vaivén** (`ping_pong = true`): `num` recorre `0, den, 0, den-1, …` (triangular).
3. **La paleta runtime**: en `num = den` es `to`; interpolada en el punto medio
   (`0x000 → 0xfff` a mitad = `0x777`).
4. **El parche**: `apply_into` registra un único parche **base** con el rango `first/count`
   y la paleta runtime.
5. **Recorte de rango**: `first` fuera de `0..31` se recorta de forma segura y `frames = 0`
   se corrige a `1`.
6. **Sin enlazar**: `apply_into` sin `bind` no registra parche (no toca memoria nula).

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/108_palette_transition
```
