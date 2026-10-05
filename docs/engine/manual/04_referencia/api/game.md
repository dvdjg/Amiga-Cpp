# Referencia — `App`, `Screen`, `Device` (`eng/api/game.hpp`)

`eng::App` es el **composition root** del juego: oculta `backend`/`GameContext` y ofrece el dominio
(bucle, escena, dibujo, entrada, audio, memoria, recursos). Se construye con el backend y tu `Game`:

```cpp
eng::App app {backend, game, backend.memory_manager()};
```

## Ciclo de vida

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `App` ctor | `App(Backend& backend, Game& game)` | `backend` (ya configurado), `game` (tu tipo) | — |
| `start` | `Expected<void,StartError> start()` | — | `Ok` o el motivo: `MemoryUnavailable`/`InvalidDisplay`/`OutOfMemory`/`BusOverBudget`/`CompositionFailed`/`AlreadyStarted`. Compone y **toma el display**. |
| `run` | `void run(u32 frames = kRunIndefinitely)` | `frames` | — (bucle hasta agotar `frames`; `app.frame()` da el índice). |
| `shutdown` | `void shutdown()` | — | — (idempotente; `run` también lo llama). |

## Display

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `set_display` | `bool set_display(const GameDisplay&)` | `display` (ancho/alto/planos/paleta) | `false` si ya arrancó o el display está ligado. |
| `display` | `const GameDisplay& display() const` | — | el display declarado. |
| `remaining_chip` | `u32 remaining_chip() const` | — | bytes Chip libres. |
| `takeover` | `void takeover()` | — | — (muestra el buffer 0 de la escena ligada; lo hace el camino planar). |

## Frame y dibujo

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `frame` | `u32 frame() const` | — | índice del frame actual. |
| `screen` | `Screen screen()` | — | **contexto de dibujo** (ver abajo). |
| `present` | `void present()` | — | — (ejecuta el plan del frame + publica la copperlist en VBlank). |
| `draw_world` | `u16 draw_world()` | — | nº de actores emitidos del `World` al plan. |
| `world` | `World<…>& world()` | — | el mundo retenido (capas + actores + cámaras). |

## `Screen` (contexto de dibujo)

`app.screen()` devuelve un `Screen` ligado al **buffer trasero** de la escena. Métodos de dominio:
`clear(color)`, `fill(Box,color)`, `frame(Box,color)`, `line(x0,y0,x1,y1,color[,op])`,
`text(x,y,str,color)`, `sprite(Sprite,x,y[,opts])`. **No** expone planos ni strides: eso lo resuelve el
`DrawTarget` de la escena. Ver `engine/include/eng/api/screen.hpp`.

## Escena y planificador (§7)

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `add_scroll_layer` | `bool add_scroll_layer(ScrollLayer<Backend>&)` | `layer` | `false` si no cabe o `begin` falla. La arranca y la conduce por frame. |
| `add_scroll_layer` | `bool add_scroll_layer(layer, LayerRole, LayerPlacement)` | rol + colocación | como arriba + **alimenta el plan de escena**. |
| `pick_scroll_engine` | `bool pick_scroll_engine(Ladder&, const RuntimeScrollGeometry&)` | ladder + geometría runtime | `false` si ninguna geometría coincide. Elige y registra el motor. |
| `scene_strategy` | `SceneStrategy scene_strategy() const` | — | `Single`/`Dpf`/`Bands`. |
| `scene_plan` | `const auto& scene_plan() const` | — | el `ScenePlan` de las capas con rol. |
| `scene_bands` | `u16 scene_bands(Span<BandSpan> out) const` | `out` (buffer) | nº de tramos escritos. |
| `scene_layout` | `bool scene_layout(RasterLayout& out)` | `out` | `false` si faltan vistas o la estrategia no mapea. Deriva el layout del plan + las vistas. |
| `present_layout` | `bool present_layout(const RasterLayout&)` | `layout` | `false` si no materializa. **Materializa y toma el display** (copperlist propia). |
| `present_scene` | `bool present_scene(Span<const Band>)` | `bands` | como arriba, desde bandas. |
| `emit_bobs_banded` | `u16 emit_bobs_banded(Bobs&, Span<const BobTarget>[, fine])` | capa de BOBs + destinos | nº de actores emitidos (usa el plan de bandas o una). |

## Memoria, recursos y servicios

| Método | Firma | Parámetros | Devuelve |
|---|---|---|---|
| `configure_memory` | `bool configure_memory(const MemoryConfig&)` | `{chip,slow,frame,fast}` en bytes | `false` si no cabe. |
| `memory_manager` | `auto& memory_manager()` | — | el `MemoryManager` del backend. |
| `resources` | `res::Budget resources()` | — | presupuesto (`used_chip`/`can_fit_chip`/`remaining_chip`). |
| `assets` | `auto& assets()` | — | el runtime de assets (caché). |
| `load_asset` | `u16 load_asset(path, size, MemoryRequest, prio)` | path, tamaño, banco pedido, prioridad | id de asset (`0` si no cabe). La E/S se completa al drenar el puerto. |
| `asset_ready`/`asset_view`/`asset_lease` | — | id | estado/vista/lease del asset. |
| `telemetry` | `debug::Telemetry telemetry()` | — | telemetría de memoria para el panel de depuración. |
| `device` | `Device<Backend> device()` | — | **escape nivel B**: Blitter/Copper/memoria. |
| `input` | `input::InputAggregator& input()` | — | entrada del frame (`pad0`/`pad1`, ratón, teclado). |
| `audio` | `auto& audio()` | — | audio del backend (SFX + música). |
| `play_music`/`stop_music` | `bool play_music(StringView name)` / `void stop_music()` | nombre de asset | `false` si no hay audio/assets. |
| `tasks` | `task::BackgroundQueue& tasks()` | — | cola de tareas de fondo. |
| `debug` | `auto& debug()` | — | overlay de depuración del backend. |

## Estados de escena (pila)

`push_scene(Scene&)` → `bool` (llama `enter`), `pop_scene()` → `bool` (llama `exit`),
`set_scene(Scene&)` → `bool` (reemplaza la pila), `scene_depth()` → `u16`. La escena superior sustituye
al `Game` en `update`/`render`; sin heap ni vtable (capacidad `kMaxScenes`).

> Fuente: `engine/include/eng/api/game.hpp`. Contrato de juego en `docs/engine/architecture/PUBLIC_GAME_API.md`.

Volver a [`api/`](README.md) · [Referencia](../README.md) · [índice del manual](../../README.md).
