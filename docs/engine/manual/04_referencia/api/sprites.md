# Referencia — `sprites` (`eng/scene/sprite_scene.hpp`)

`eng::SpriteScene<MaxActors>` es la **fachada de sprites de nivel A**: el juego da de alta **actores**
(`ActorDesc`) y llama `emit`; el engine **compone** (HW sprites + `SpriteAllocator` + BOB fallback +
Copper) sin que el juego declare buffers de trabajo ni un `FramePlan`. Es la fachada del sistema de
objetos NES (8 sprites/línea + overflow).

## Métodos

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `add` | `ActorId add(const ActorDesc&)` | visual + posición + prioridad + … | id del actor (`ActorId{}` inválido si está lleno o no tiene contenido). |
| `remove` | `bool remove(ActorId)` | id | `true` si lo quitó. |
| `count` | `u16 count() const` | — | nº de actores. |
| `clear` | `void clear()` | — | — (vacía el almacén). |
| `emit` | `SpriteLoadReport emit(FramePlan& plan, const ActorEmitContext& ctx[, Ref<copper::Plan>])` | plan del frame, contexto y Copper opcional | reporte: `hw`, `degraded`, `bob`, `copper_intents`, `ok`. |
| `placements` | `Span<const HwSpritePlacement> placements() const` | - | colocaciones HW del ultimo `emit`. |
| `store`/`result` | `ActorStore<...>& store()` / `const SpriteComposeResult& result()` | - | acceso al almacen / resumen compuesto interno. |

## Uso

```cpp
eng::SpriteScene<64> sprites;                        // hasta 64 actores (NES OAM)
sprites.add_direct({.visual = nave, .x = 100, .y = 40, .sprite_priority = 1});
// render:
auto r = sprites.emit(plan, ctx);                    // los BOB van al plan
m_manager.apply(sprites.placements().data(), r.hw);        // HW sprites a la copperlist
```

Por debajo: `scene::compose_sprites` + `SpriteAllocator` + `emit_bob_fallbacks` (ver
`engine/include/eng/scene/actor_sprite.hpp`). La demo `062_sprite_scene` ya usa `SpriteDirector`; la cobertura de esta API interna es `HOST-391`. Diseño: `docs/engine/architecture/OBJECT_SYSTEM.md`.

## Fachada por intención: `SpriteDirector`

`eng::scene::SpriteDirector` (`engine/include/eng/scene/sprite_director.hpp`) es la fachada nueva: el juego registra contenido (`VisualId`/`CompositeId`), declara `SpriteHint` y posición, y recibe un `SpriteLoadReport` único (canales, scanline, BOB, Copper, paleta, gap y emisores). Compone simple y compuesto con un ledger compartido, aplica el gap de 1 línea y permite `BandPalette` con `top + scroll_y`. `SpriteScene`/`CompositeScene` quedan como implementación interna/legacy en `eng/scene/`. Test: `HOST-485`. Detalle: `docs/engine/architecture/SPRITE_DIRECTOR.md`.

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
