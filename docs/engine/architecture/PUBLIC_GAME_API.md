# API de juego (borrador evolutivo)

Propuesta del **API que consume el desarrollador de juego** sobre los módulos que **ya existen**.
Es evolutivo: se escribe para lo disponible y se adapta cuando lleguen los módulos que faltan
(planner, mundo/capas, assets cocinados, UI). Los principios están en
[PUBLIC_API.md](PUBLIC_API.md) §1.1: **intuitivo, simple y sin restringir funcionalidad**.

> Este documento fija el **objetivo** y el **mapeo** desde el API interno actual; es la referencia
> para «¿cómo lo pediría un juego?» al tocar cada módulo.

## 1. Estado contrastado: fachada útil, objetivo de juego pendiente

La tabla distingue piezas **disponibles en la fachada** de la experiencia final del API de juego.
No implica que el developer ya pueda crear un juego completo solo con vocabulario de dominio:
para fondos, memoria de escena y varios flujos de asset todavía se baja a `Device`/`Scene` o se
construye materialización en la app. El diagnóstico actualizado está en
[`ROADMAP_API_COHERENCE.md`](ROADMAP_API_COHERENCE.md) §2.1.

| Área | Existe hoy (interno) | Falta (objetivo) |
|---|---|---|
| Bucle | `eng::App` oculta `backend`/`GameContext` en `init/update/render` | Un ejemplo 2D completo de nivel A, sin configuración manual de memoria/escena |
| Display/escena | `App::start()` compone/posee el display y materializa fondos Fill desde `World`, aplicando cámara; `Screen::bitmap` dibuja un bitmap planar de asset | Materializador de tilemap y planner de composición de múltiples capas |
| Dibujo | `Screen` ofrece primitivas, `sprite` y copia de bitmap planar por asset | Retirar `Screen::target()` de la ruta normal; `Screen::blit` aún expone stride/planos/shift/operación |
| Entrada | `input::InputAggregator` (estado por frame) | Fachada de acciones + (mini-SO de mensajes, documentado) |
| Tareas de fondo | `task::BackgroundQueue` | Fachada `tasks()` |
| Blitter/efectos | `FramePlan` (jobs), `graphics::blitter_state` (`OrBob`/`LineEor`/`C2p4`) | Efectos como concepto (`world.add_effect`) |
| Copper chunky | `composition::CopperChunkyLayer` (`attach`/`begin_frame`/`row`/`end_frame`) | Efecto de alto nivel |
| Paleta | `eng::Palette32` | — |
| Actores/sprites | `eng::scene::ActorStore`/`Actor` | Representación elegida por el engine |
| Audio | `App::audio()` y `AudioSystem` | Fachada actual expone el `AudioSystem` de backend; simplificar selección/recursos en otro bloque |
| UI | Documentada (`GUI_LIBRARY.md`), sin implementar | `eng::ui` |

## 2. El API propuesto (para lo que existe)

```cpp
// main.cpp de un juego
eng::amiga::AmigaBackend backend {};
if (!backend.configure_game_memory()) return; // perfil editable seleccionado desde HwInfo
MyGame game {};
eng::App app {backend, game, backend.memory_manager()};
if (!app.start()) return;       // compone y posee el display declarado por GameDisplay
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
| `app.input()` | estado de entrada del frame | `input::InputAggregator` (vía `poll_input`) |
| `app.audio()` | audio del backend (SFX + música) | `backend.audio()` |
| `app.tasks()` | tareas de fondo | `context.background` |
| `app.start()` | compone, posee e instala el display antes de `run()` | `scene::compose` + `Scene::takeover` |
| `app.add_background(id, depth, bounds, color)` | declara una región Fill opaca en coordenadas de mundo; `App` la traslada con cámara y presenta antes de `Game::render` | `scene::World` + `Screen::fill` |
| `app.shutdown()` | detiene DMA/presentación antes de liberar la escena | ciclo de vida interno de `Scene` + backend |
| `app.present()` | publica el frame (copper/swap) | `scene.commit()`/`present()` |

**Efectos** (borrador, `eng/api/effects.hpp`): el juego pide el efecto, no la secuencia de
primitivas de Copper.

```cpp
eng::effects::CopperChunky fx;                                   // display sin bitplanes
fx.init(scene, memory, limits, {.cols = 36, .rows = 64});
// por frame:
fx.begin_frame(scene);
for (y...) { u16* p = fx.row(y); /* colores */ }
fx.end_frame(scene, backend);                                    // flip + install
```

`Screen` es la envoltura de dibujo disponible, pero aún no es el contexto de dominio final:
`Screen::target()` devuelve `field::DrawTarget&`, y `Screen::blit` recibe layout/strides/planos.
Para imágenes planares registradas con `Assets::add_bitmap`, `Screen::bitmap(assets.bitmap(name), box)`
usa la geometría y el layout asociados al asset; el acceso al target y el blit genérico se consideran
fugas conocidas.

```cpp
eng::Screen& s = app.screen();
s.clear(0);
s.fill({10, 10, 40, 12}, color);            // inmediato (rasterizador)
s.fill_box({10, 24, 40, 12}, color);        // diferido (Blitter, D=A sin fetch de D)
s.frame({8, 8, 100, 40}, color);
s.line(0, 0, 319, 0, color);
s.text(4, 4, "hola", color);
s.sprite(...);            // cuando exista el sistema de objetos
app.present();            // ejecuta el plan del frame y publica
```

### 2.0 Estados de escena — `App::push_scene`/`pop_scene`/`set_scene`

Un juego con estados (title→game→gameover) apila **escenas** sobre el `Game`. Mientras haya una escena
en la pila, su `update`/`render` **sustituyen** a los del `Game`; `enter`/`exit` se llaman en las
transiciones (y son opcionales). Sin heap ni vtable: capacidad fija `kMaxScenes` y despacho por thunks.
Ver `tests/host/ui/240_app_scenes`.

```cpp
struct Title {
    void enter(eng::App& app) { app.play_music("title"); }   // música por escena (§2+§6)
    void update(eng::App& app) { if (app.input().pad0.fire) app.set_scene(m_menu); }
    void render(eng::App& app) { app.screen().text(8, 8, "PULSA FUEGO"); }
};
```

## 2.1 Contrato de las abstracciones de juego (dibujo, color, scroll, recursos)

Estas cuatro familias son las que el port de la demo 213 midió como **huecos** (`PUBLIC_API.md` §8.1). El contrato fija **nombres de dominio, tipos fuertes y manejo de error**; la implementación envuelve lo ya existente (`Bob`/`bob_draw`, `Palette32`/`util::palette_*`, `Camera2D`/`TileScrollDriver`, `AssetCache`/`DynLoader`) sin introducir una segunda verdad.

Reglas comunes a todas (previenen errores por construcción):

- **Objetivo: sin punteros ni offsets en la firma del juego**: el nivel A no ve `u16*`, `u8*`, strides, número de planos ni módulos. El estado actual no lo satisface aún: `Screen::target()`, `Screen::blit` y `Device` exponen rutas de transición/de bajo nivel. Eso se clasifica en `ROADMAP_API_COHERENCE.md` §2.1.
- **Identidad fuerte**: `SpriteId`, `ColorIndex`, `LayerId`, `AssetHandle<T>` son tipos distintos, no `u8`/`u16` sueltos: no se puede pasar el índice de color donde va un canal ni un handle de música donde va uno de sprite.
- **Error sin excepciones**: `[[nodiscard]] bool` para lo que puede fallar por presupuesto; los *setters* de valor (color, scroll) no fallan si ya hay sitio, o devuelven `bool` si el recurso está lleno.
- **Una llamada por intención**, configuración por *designated initializers* con defectos.

### 2.1.1 Dibujo de objetos — `Sprite` y `screen.sprite(...)`

Un `Sprite` es un **asset cocinado**: geometría (ancho, alto, planos), el bitmap de frames y, opcionalmente, la máscara de cookie-cut, todo junto. Como la geometría viaja con el asset, es **imposible** dibujar con un número de planos o un stride que no corresponda al bitmap.

```cpp
eng::Sprite nave = app.load_sprite("assets/nave.bpl").value();   // asset cocinado
// render:
s.sprite(nave, 100, 40);                        // frame 0, cookie-cut, anclado arriba-izquierda
s.sprite(nave, 100, 40, {.frame = 3});          // un frame concreto
s.sprite(nave, x, y, {.anchor = eng::Anchor::Center, .erases = eng::Erase::Auto});
```

`screen.sprite` reemplaza hoy a `graphics::bob_draw(plan, bob, frame, x, y, target)`; el `BobTarget` (base, `row_bytes`, `plane_bytes`, planos, layout) se lo da el **contexto de dispositivo** (`Scene::bob_target()` a través del `DrawTarget`), no el juego. Implementado en `eng/graphics/sprite_asset.hpp` (`Sprite`) + `Scene::bob_target()` + `Screen::sprite()`; gate en la demo **117_bobs3d** y HOST-324.

### 2.1.2 Color y paleta — `Color`, `ColorIndex`, `Palette`

`Color` es un RGB444 con constructores con nombre (valida 0..15); `ColorIndex` es el índice `0..31`. La `Palette` da los métodos que hoy no existen (`set`/`get`/`mix`/`fade`) y puede ser la **paleta de una capa** o la global.

```cpp
s.palette().set(eng::ColorIndex {1}, eng::Color::rgb(15, 8, 0));
s.palette().fade(1, 2);                          // al 50 % hacia negro (util::palette_scale)
s.palette().mix(fondo, niebla, frame, 100);      // transición (util::palette_lerp)
eng::Color c = s.palette().get(eng::ColorIndex {1});
```

Por debajo escribe un `Palette32` y lo publica con `FramePlan::add_base_palette_patch` (o `PaletteTransitionEffect`/`PaletteCycleEffect` si se pide animación); reutiliza `eng::util::palette_lerp`/`palette_scale`/`lerp444` y **nunca** expone `color_register`.

### 2.1.3 Scroll y cámara — `Camera` y `Layer`

Una `Camera` es la ventana de la capa al mundo; `scroll_x`/`scroll_y` son su posición, no palabras de `BPLCON1`. Una `Layer` agrupa un bitmap de fondo (o tilemap) con su cámara y su prioridad; un `Actor`/`Sprite` puede ser promovido a capa (Jim Power, `SCENE_AND_RESOURCES.md`).

```cpp
eng::Layer& fondo = app.world().add_layer({.id = "fondo", .depth = 0});
fondo.camera().scroll_x = 128;                   // mueve la vista, no un registro
fondo.camera().scroll_x += 2;                    // avance por frame
s.sprite(nave, 100, 40);                         // los objetos van en coordenadas de pantalla
```

Reutiliza `scene::Camera2D` (`virtual_scene.hpp`) y `TileScrollDriver`/`FineScroll`; el `scroll_x` de la cámara es lo que hoy se parchea a mano en `BPLCON1` (en la 213, un `PatchHandle`). **Estado (aditivo):** `eng/scene/world.hpp` da `app.world().add_layer("fondo", 0)` → `Ref<Layer>` (anulable si el mundo está lleno) y `layer->camera().scroll_x`/`set_scroll_x(...)`; el **planner** que materializa cada capa (playfield/tilemap/efecto) y el reparto de recursos se construyen encima. Gates: `214_app_sprite` (cámara de capa moviendo el sprite, HOST-327) y las demos de scroll por tiles.

### 2.1.4 Recursos — `app.load<T>(...)` y presupuesto

El `load<T>` de este contrato es **objetivo, no API implementada**: declara, carga y cachea un asset
tipado, pero hoy `App::load` recibe path+tamaño+`MemoryRequest` y devuelve `AssetId`; no decodifica
`Sprite`/`Music` ni devuelve `AssetHandle<T>`.

```cpp
auto mod  = app.load<eng::Music>("assets/testmod.p61");   // -> AssetHandle<Music> / Result
auto spr  = app.load<eng::Sprite>("assets/abyss.bob");
sprite->draw(s, x, y);
app.resources().used_chip();                      // presupuesto consultable antes de pedir
```

La base disponible es `AssetCache`/`AssetRuntime` bytes-only, `res::load<Tag>`, `res::load_file<Tag>` y
`Budget`. La fachada también da acceso a `assets()`, `load_asset(path,size,request,prio)`, `asset(id)` /
`asset_view(id)` y leases sin un tipo de dominio resuelto. `Assets::add<Tag>(name, data, size)` copia
blobs registrados en setup; `Assets::create<Tag>(size)` devuelve un `Block<Tag>` para reservas
manuales. La decodificación tipada y el camino asíncrono completo con asset de juego siguen
pendientes; no tratar el boceto `app.load<Music>(path)` como llamada real.

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
| `composition::CopperChunkyLayer` | `effects::CopperChunky` (`eng/api/effects.hpp`) |
| `eng::scene::Actor`/`ActorStore` | `world.add_actor({...})` |
| `graphics::FramePlan` (jobs de Blitter) | interno del `Screen`/planner |
| `backend.audio()` | `app.audio()` |
| `graphics::bob_draw` + `BobTarget` | `Sprite` (`graphics/sprite_asset.hpp`) + `screen.sprite(...)` (§2.1.1) |
| `Palette32` + `copper::color_register` + `emit_palette` | `screen.palette().set/mix/fade` (§2.1.2) |
| `PatchHandle` de `BPLCON1` + `Camera2D`/`TileScrollDriver` | `layer.camera().scroll_x` (§2.1.3) |
| `incbin` + `Block<Tag>` + `memcpy` a Chip | `app.load<T>(...)` + `resources()` (§2.1.4) |

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

1. **`App` + `Screen`** sobre `Engine`/`Scene`/`Surface`/`FramePlan`: **hecho** (HOST-234);
   migrar una demo de juego al `App` (p. ej. `204_collide_game`) es el siguiente paso.
2. **`input()`/`audio()`/`tasks()`/`frame()`** expuestos por `App`: **hecho** (wrappers baratos;
   `input()` se rellena con `poll_input` hasta que el mini-SO de mensajes lo sustituya).
3. **Efectos** como concepto: **hecho** para copper-chunky (`effects::CopperChunky`, usado por
   082/083); faltan degradados y otros.
4. **Actores** (`ActorStore`) tras la representación elegida por el engine.
5. **UI** (`eng::ui`) cuando se implemente.
6. **`Sprite` + `screen.sprite(...)`** (§2.1.1): **hecho** en `eng/graphics/sprite_asset.hpp` — `Sprite` envuelve `Bob`, `Scene::bob_target()` prepara el destino para el `DrawTarget` y `Screen::sprite(...)` lo usa. Gate: demo **117_bobs3d** (`Sprite::draw`) + demo **214_app_sprite** (`app.screen().sprite(...)` con `eng::App`) + HOST-324.
7. **`Palette` (`set`/`mix`/`fade`)** (§2.1.2): envolver `Palette32` + `util::palette_*` + `add_base_palette_patch`; gate host de la aritmética de color.
8. **`Camera`/`Layer` con `scroll_x`** (§2.1.3): unificar `Camera2D` + `TileScrollDriver`/`FineScroll` tras una capa con cámara.
9. **`app.resources()` + `res::load<T>`** (§2.1.4): **presupuesto** (HOST-325), **carga síncrona tipada** `res::load<Tag>` (HOST-326, gate en la demo 213), **carga desde fichero** `res::load_file<Tag>` (HOST-328), **backend Amiga de `AssetCache`** (HOST-330) y **runtime** `res::AssetRuntime` + enganche en `AmigaBackend::assets()`/`app.load_asset`/`app.route_resource_io` (HOST-331) **hechos**; falta la **decodificación tipada** `app.load<T>("path")` y una demo de carga asíncrona con assets de disco.
10. **Consumidores migrados**: `204_collide_game` usa `app.screen()` y `app.device().blitter_*`; `086_bob_objects` usa la fachada `App` pero llama a `device().memory_manager()`, crea su propio `copper::Plan` y ejecuta `FramePlan`. Son demos técnicas del nivel B, no demostración de que el API de juego de nivel A esté terminado.
11. **`World`/`Layer` + planner** (§2.1.3): el contenedor **hecho** (`eng/scene/world.hpp`: capas + `ActorStore` + `emit`; `app.world()`, HOST-327/329) y la **entrada del planner de actores** `app.draw_world()` (emite el mundo al plan del frame; gate `214_app_sprite`, que dibuja un sprite por `screen.sprite` y un actor por `draw_world`); falta la **materialización de capas** (playfield/tilemap/efecto) y el reparto de recursos (`SCENE_AND_RESOURCES.md`).
12. **Servicios de hardware en `App`**: **hecho** — `app.wait_blitter()`, `app.install_raster(scene)`, `app.blitter_clear/or_bobs/collide(...)`, `app.execute_frame_plan(plan)`, `app.takeover_copper(plan)`/`app.commit_copper(plan)`, `app.scene()`/`app.copper()`/`app.copper_scheduler()` (reenvían al backend si lo soporta); gates en `204_collide_game` y `086_bob_objects`. Faltan más servicios (sprites/copper por objeto de alto nivel) según los pidan las demos.

**Criterio actual de limpieza:** parcial. `api.hpp` incluye `App`/`Screen`, hay operaciones de dibujo
por dominio y un escape agrupado en `Device`; pero un juego aún configura memoria, liga una `Scene`,
puede obtener `DrawTarget` vía `Screen::target()`, y el `World` no materializa capas de fondo/tilemap.
Por tanto es una base útil y evolutiva, no todavía la fachada simple prometida para un juego completo.

## 6. Referencias

- Principios y regla prioritaria: [PUBLIC_API.md](PUBLIC_API.md) §1.1.
- Contexto de dibujo y fuentes: `engine/include/eng/field/surface.hpp`.
- Revisión de estructura y decisiones: [ENGINE_STRUCTURE_REVIEW.md](ENGINE_STRUCTURE_REVIEW.md).
- UI (documentada): [GUI_LIBRARY.md](GUI_LIBRARY.md).
