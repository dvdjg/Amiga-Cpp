# Referencia — `world_render` (`eng/api/world_render.hpp`)

Puente que **materializa el mundo retenido** (`World`) sobre un `Screen`: traslada las capas por la
cámara de cada una, las recorta al viewport y las dibuja. Es lo que `App` ejecuta **antes** del
`render` del juego (`app.draw_world()`).

## Funciones

| Función | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `materialize_fill_layers`*(interno)* | `World::materialize_fill_layers(fn, width, height)` | callable `(Box, color)` por capa Fill | `false` si algo no cupo. |
| `materialize_bitmap_layers` | `bool materialize_bitmap_layers(World&, u16 width, u16 height, Screen)` | mundo, viewport, contexto | `false` si una fila no cupo. Emite las capas `Bitmap` como filas indexadas (run-length por fila). |
| `materialize_world_layers` | `bool materialize_world_layers(World&, u16 width, u16 height, Screen)` | mundo, viewport, contexto | `false` si algo no cupo. Orden de composición: primero `Fill` (fondo), después `Bitmap`. |

## Cómo encaja

```
  app.draw_world()
     └─ materialize_world_layers(world, w, h, screen)
          ├─ capas Fill   -> screen.fill(bounds, color)       (fondo, en orden de profundidad)
          └─ capas Bitmap -> filas run-length -> screen.fill  (imágenes indexadas)
```

El `World` es una **plantilla**, de modo que este puente sirve a cualquier `World` que exponga
`materialize_fill_layers`/`materialize_bitmap_layers` (contrato mínimo, sin arrastrar `scene/world.hpp`).

> Fuente: `engine/include/eng/api/world_render.hpp`. Ver también [`game.md`](game.md) (`draw_world`).

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
