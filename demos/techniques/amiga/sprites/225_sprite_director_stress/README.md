# 225 — SpriteDirector stress

Demo técnica de la fachada por intención `eng::scene::SpriteDirector`: contenido simple y compuesto, par *attached*, tira horizontal, política BOB, gap de 1 línea y `BandPalette` con `top + scroll_y`.

## Qué enseña

- `VisualStore`/`CompositeStore` y `SceneAssets` con handles generacionales (sin `const Visual*`).
- `SpriteDesc` + `SpriteHint`: posición de pantalla, política de materialización, reutilización, canal, rango y tira.
- Composición combinada: `CompositeScene` primero, `SpriteScene` después con un `SpriteChannelLedger` compartido.
- `SpriteAllocator` con gap de 1 línea; par *attached* cocinado en Chip; tira de dos tramos; BOB forzado con hoja planar.
- `BandPalette` en la franja baja (playfield rojo/amarillo) y `SpritePaletteEvent` de sprites.
- HUD de depuración del host (`app.debug().text`) con nombre + número de demo, frame renderizado y fps; el viewport no lleva HUD.

## Búsqueda previa

Se revisó el arte previo antes de escribir la demo: `engine/include/eng/scene/sprite_director.hpp` (`SpriteDirector`, `SpriteDesc`, `SpriteHint`, `SpriteLoadReport`), `engine/include/eng/api/sprites.hpp` (`SpriteScene`), `engine/include/eng/api/objects.hpp` (`CompositeScene`), `engine/include/eng/graphics/sprite_manager.hpp` (`SpriteManager::emit_placements_into`), `engine/include/eng/graphics/sprite_allocator.hpp` (`SpriteAllocator`) y `engine/include/eng/graphics/sprite_line_layer.hpp` (modo `PerLine` avanzado). La demo usa la fachada nueva; no reimplementa el reparto ni el emisor.

## Ejecución

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress --debug
bash tools/run/run-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress
```

La evaluación visual está en [`src/VISION.md`](src/VISION.md), la intención en [`src/INTENT.md`](src/INTENT.md) y las medidas en [`VALIDATION.md`](VALIDATION.md).
