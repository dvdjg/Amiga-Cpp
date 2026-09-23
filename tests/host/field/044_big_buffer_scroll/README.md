# HOST-044 — `BigBufferScroll` (estrategia trivial "escena ya dibujada")

Valida `eng::field::BigBufferScroll` (`engine/include/eng/field/scroll_engine.hpp`), la estrategia
de scroll **trivial** de Fase 2: no hay anillo ni banda de staging que rellenar; el único estado es
el offset de cámara sobre el buffer completo (el display mueve el puntero/BPLxPT).

## Qué fija

- `step(dx)` avanza el offset y **clampa** al rango `[min_pos, max_pos]` cuando `clamp = true`.
- `clamp = false` (anillo) no recorta.
- `reset(p)` fija la posición.
- Es un POD sin sobrecoste (solo enteros).

## Cómo corre

```bash
bash tools/run-host-tests.sh tests/host/044_big_buffer_scroll
```

## Relación

- Contraste con `ScrollEngine` (corkscrew/XYLimited), que sí emite blits.
- Vocabulario de ejes/dirección: `AxisPolicy` / `DirectionPolicy` (`xlimited.hpp`).
