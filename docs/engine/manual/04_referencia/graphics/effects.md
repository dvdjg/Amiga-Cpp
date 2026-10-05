# Referencia — `graphics/effects/`

Efectos de escena **sin asignaciones dinámicas**: escriben en el `FramePlan` o emiten intenciones de Copper, y el driver decide la materialización.

## `Rotozoom` — `effects/rotozoom.hpp`

`Rotozoom` (`rotozoom.hpp:47`) describe ángulo/zoom/offset (16.16) y `RotozoomSteps` (`:58`) los pasos `(u, du, v, dv)` precalculados. `rotozoom_steps<TW, TH>(r, w, h)` (`:70`) los calcula en 8.8 (para que los productos quepan en 32 bits: el runtime m68k **no** enlaza `__muldi3`); `rotozoom_into<TW, TH>(tex, r, dst, w, h)` (`:90`) muestrea la textura indexada `TW×TH` hacia un buffer de índices `w×h` (1 B/píxel, `w` múltiplo de 16 para el C2P). Pero por píxel: el efecto «chunky» clásico.

## `RasterGradientEffect` — `effects/raster_gradient.hpp`

`RasterGradientEffect` (`raster_gradient.hpp:45`) pinta un **degradado por banda** con colores clave: `configure(range)` (`:51`, recorta `bands`/`band_height`/`first` a `max_bands`/`max_keys`), `set_keys(keys)` (`:69`), `set_cyclic(bool)` (`:80`), `set_phase(phase)` (`:86`) y `fill_intents(out)` (`:94`), que rellena `out` con una intención `PaletteLine` por banda (los colores viven en un buffer propio, estable hasta el siguiente `fill_intents`). Es la forma barata del *raster gradient*: en vez de un MOVE por línea, se calcula una tabla y se emiten bandas.

## Paletas animadas

| Efecto | Qué hace |
|---|---|
| `PaletteCycleEffect` (`effects/palette_cycle.hpp:43`) | Rota un **tramo** de color: `configure(PaletteCycleRange)` (`:50`), `update(frame_index)` (`:70`) avanza la fase, `apply(source, destination)` (`:79`) copia aplicando la rotación (sin modificar la paleta original — el motor es *retained-mode*), `bind_source(source)` (`:100`) liga la fuente y produce su propia paleta runtime (aportada al plan en `apply_into`). |
| `PaletteTransitionEffect` (`effects/palette_transition.hpp:45`) | Fundido **`from` → `to`**: `configure(range)` (`:51`), `bind(from, to)` (`:72`), `update(frame_index)` (`:80`) calcula `num/den` (triangular si `ping_pong`, o `0..den` y se queda). `runtime_palette()` (`:98`) da la paleta derivada. |

Volver al [índice de `graphics/`](README.md) · [Referencia](../README.md) · [manual](../../README.md).
