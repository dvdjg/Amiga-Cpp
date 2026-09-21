# API de juego (borrador evolutivo)

Propuesta del **API que consume el desarrollador de juego** sobre los módulos que **ya existen**.
Es evolutivo: se escribe para lo disponible y se adapta cuando lleguen los módulos que faltan
(planner, mundo/capas, assets cocinados, UI). Los principios están en
[PUBLIC_API.md](PUBLIC_API.md) §1.1: **intuitivo, simple y sin restringir funcionalidad**.

> Este documento fija el **objetivo** y el **mapeo** desde el API interno actual; es la referencia
> para «¿cómo lo pediría un juego?» al tocar cada módulo.

## 1. Estado: qué hay y qué falta

| Área | Existe hoy (interno) | Falta (objetivo) |
|---|---|---|
| Bucle | `eng::Engine` + `GameModule` (`init/update/render`), `run_frames` (IRQ) / `run_frames_polling` | Fachada `App` que oculte `backend`/`GameContext` |
| Display/escena | `scene::compose` + `Scene` + `SceneResources` (modos Standard/HAM/EHB/DPF/CopperChunky) | `World`/capas + planner (elige modo) |
| Dibujo | `field::Surface`, `field::DrawTarget`, `graphics::FramePlan`, `field::Rasterizer` | Contexto de dibujo de alto nivel (`Screen`/pincel) |
| Entrada | `input::InputAggregator` (estado por frame) | Fachada de acciones + (mini-SO de mensajes, documentado) |
| Tareas de fondo | `task::BackgroundQueue` | Fachada `tasks()` |
| Blitter/efectos | `FramePlan` (jobs), `graphics::blitter_state` (`OrBob`/`LineEor`/`C2p4`) | Efectos como concepto (`world.add_effect`) |
| Copper chunky | `composition::CopperChunkyLayer` (`attach`/`begin_frame`/`row`/`end_frame`) | Efecto de alto nivel |
| Paleta | `eng::Palette32` | — |
| Actores/sprites | `eng::scene::ActorStore`/`Actor` | Representación elegida por el engine |
| Audio | `eng::audio::AudioSystem`, mixer/música | Fachada `audio()` |
| UI | Documentada (`GUI_LIBRARY.md`), sin implementar | `eng::ui` |

## 2. El API propuesto (para lo que existe)

```cpp
// main.cpp de un juego
eng::amiga::MinimalBackend backend {};
MyGame game {};
eng::App app {backend, game};   // junta bucle + pantalla + entrada + tareas
app.run();                       // bucle por defecto (interrupt-driven, sin polling)
```

```cpp
struct MyGame {
    void init(eng::App& app);     // configura la escena y los recursos (una vez)
    void update(eng::App& app);   // lógica del frame (no dibuja)
    void render(eng::App& app);   // dibujo del frame
};
```

`App` ofrece el dominio del juego, sin hardware:

| Llamada | Qué da | Reemplaza hoy a |
|---|---|---|
| `app.frame()` | índice de frame | `context.frame.frame_index` |
| `app.screen()` | **contexto de dibujo** (`Screen`) | `scene.surface()` + `FramePlan` + `Rasterizer` |
| `app.input()` | estado de entrada del frame | `input::InputAggregator` (vía backend) |
| `app.tasks()` | tareas de fondo | `context.background` |
| `app.scene()` | la escena (para configurarla en `init`) | `scene::Scene` |
| `app.present()` | publica el frame (copper/swap) | `scene.commit()`/`present()` |

`Screen` (contexto de dibujo de alto nivel, análogo al `RastPort`): **la app nunca ve planos,
`FramePlan` ni `Rasterizer`**.

```cpp
eng::Screen& s = app.screen();
s.clear(0);
s.fill({10, 10, 40, 12}, color);
s.frame({8, 8, 100, 40}, color);
s.line(0, 0, 319, 0, color);
s.text(4, 4, "hola", color);
s.sprite(...);            // cuando exista el sistema de objetos
app.present();            // ejecuta el plan del frame y publica
```

## 3. Mapeo interno → público (guía al tocar cada módulo)

| Interno (hoy) | Público (objetivo) |
|---|---|
| `Engine::run_frames` + `GameContext` | `App::run()` |
| `scene::compose(scene, memory, res, limits, stages...)` | `app.init_display(...)` / `world` + planner |
| `scene::Scene::surface()` + `field::DrawTarget` + `FramePlan` | `app.screen()` (`Screen`) |
| `field::RasterOp` | parámetro de `Screen` (nombrado) |
| `backend.memory().chip` / `Block<Tag>` | gestión de recursos del `App`/`World` |
| `task::BackgroundQueue` | `app.tasks()` |
| `input::InputAggregator` | `app.input()` |
| `composition::CopperChunkyLayer` | `world.add_effect(CopperChunky{...})` |
| `eng::scene::Actor`/`ActorStore` | `world.add_actor({...})` |
| `graphics::FramePlan` (jobs de Blitter) | interno del `Screen`/planner |

## 4. Reglas de diseño del API público

- **Dominio del juego, no del chipset**: nombres como `Screen`, `Sprite`, `Layer`, `Scroll`,
  `Color`, `Actor`; nunca `Plane`, `BPLCON`, `Blitter`, `FramePlan`, `Rasterizer`, `Scheduler`.
- **Una llamada por intención**: lo habitual en una línea; la configuración avanzada por
  *designated initializers* con valores por defecto.
- **Sin restricciones**: cualquier capacidad del engine debe poder pedirse desde el API (si no,
  falta abstracción). La simplicidad no recorta.
- **Tipos fuertes** en la frontera (`ColorIndex`, `LayerId`, `Box`); **sin punteros ni offsets**.
- **Errores sin excepciones**: `[[nodiscard]]` + motivo en `init`; `frame` no falla si la escena
  era válida.
- **Evolutivo**: se escribe para lo que existe; al llegar un módulo se amplía el API y se adaptan
  las demos, sin dejar dos verdades.

## 5. Plan de adopción (evolutivo)

1. **`App` + `Screen`** sobre `Engine`/`Scene`/`Surface`/`FramePlan` (borrador en
   `eng/api/game.hpp`): migrar una demo sencilla (p. ej. `204_collide_game`) como prueba de que
   oculta `backend`/`GameContext`/`FramePlan`.
2. **`input()`/`tasks()`/`frame()`** expuestos por `App` (wrappers baratos).
3. **Efectos** como concepto (`CopperChunky`, degradados, etc.) sobre `world`/`App`.
4. **Actores** (`ActorStore`) tras la representación elegida por el engine.
5. **Audio/UI** cuando sus módulos estén listos.

Mientras tanto, el API de `eng/api/api.hpp` (fachada de tipos) sigue siendo la puerta de lo
existente; `App`/`Screen` lo envuelven para el caso de juego.

## 6. Referencias

- Principios y regla prioritaria: [PUBLIC_API.md](PUBLIC_API.md) §1.1.
- Contexto de dibujo y fuentes: `engine/include/eng/field/surface.hpp`.
- Revisión de estructura y decisiones: [ENGINE_STRUCTURE_REVIEW.md](ENGINE_STRUCTURE_REVIEW.md).
- UI (documentada): [GUI_LIBRARY.md](GUI_LIBRARY.md).
