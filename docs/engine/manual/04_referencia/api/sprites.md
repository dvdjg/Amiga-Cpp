# Referencia — `sprites` (`eng/api/sprites.hpp`)

`eng::SpriteScene<MaxActors>` es la **fachada de sprites de nivel A**: el juego da de alta **actores**
(`ActorDesc`) y llama `emit`; el engine **compone** (HW sprites + `SpriteAllocator` + BOB fallback +
Copper) sin que el juego declare buffers de trabajo ni un `FramePlan`. Es la fachada del sistema de
objetos NES (8 sprites/línea + overflow).

## Métodos

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `set_budget` | `void set_budget(const RepresentationBudget&)` | `{sprite_channels, bob_budget_words, layer_slots}` | — (llamar **antes** de `add`; consume al dar de alta). |
| `add` | `ActorId add(const ActorDesc&)` | visual + posición + prioridad + … | id del actor (`ActorId{}` inválido si está lleno o no tiene contenido). |
| `remove` | `bool remove(ActorId)` | id | `true` si lo quitó. |
| `count` | `u16 count() const` | — | nº de actores. |
| `clear` | `void clear()` | — | — (vacía el almacén). |
| `emit` | `SpriteComposeResult emit(FramePlan& plan, const ActorEmitContext& ctx[, Ref<copper::Plan>])` | plan del frame, contexto (cámaras/clip/targets), Copper opcional | resumen: `sprites` (HW), `degraded` (no cupieron → BOB), `bobs`, `copper`, `ok`. |
| `placements` | `Span<const HwSpritePlacement> placements() const` | — | colocaciones HW del último `emit` (las `result().sprites` primeras). |
| `store`/`result` | `ActorStore<…>& store()` / `const SpriteComposeResult& result()` | — | acceso al almacén / al resumen. |

## Uso

```cpp
eng::SpriteScene<64> sprites;                        // hasta 64 actores (NES OAM)
sprites.set_budget({8u, 60000u, 0u});                // 8 canales HW, 60k palabras de BOB, 0 capas
sprites.add({.visual = nave, .x = 100, .y = 40, .sprite_priority = 1});
// render:
auto r = sprites.emit(plan, ctx);                    // los BOB van al plan
m_manager.apply(sprites.placements().data(), r.sprites);   // HW sprites a la copperlist
```

Por debajo: `scene::compose_sprites` + `SpriteAllocator` + `emit_bob_fallbacks` (ver
`engine/include/eng/scene/actor_sprite.hpp`). Demo: `demos/features/engine/amiga/062_sprite_scene`;
gate host: `HOST-391`. Diseño: `docs/engine/architecture/OBJECT_SYSTEM.md`.

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
