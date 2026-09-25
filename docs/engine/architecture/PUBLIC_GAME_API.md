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
eng::amiga::AmigaBackend backend {};
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
| `app.input()` | estado de entrada del frame | `input::InputAggregator` (vía `poll_input`) |
| `app.audio()` | audio del backend (SFX + música) | `backend.audio()` |
| `app.tasks()` | tareas de fondo | `context.background` |
| `app.scene()` | la escena (para configurarla en `init`) | `scene::Scene` |
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

## 2.1 Contrato de las abstracciones de juego (dibujo, color, scroll, recursos)

Estas cuatro familias son las que el port de la demo 213 midió como **huecos** (`PUBLIC_API.md` §8.1). El contrato fija **nombres de dominio, tipos fuertes y manejo de error**; la implementación envuelve lo ya existente (`Bob`/`bob_draw`, `Palette32`/`util::palette_*`, `Camera2D`/`TileScrollDriver`, `AssetCache`/`DynLoader`) sin introducir una segunda verdad.

Reglas comunes a todas (previenen errores por construcción):

- **Sin punteros ni offsets en la firma**: el juego nunca ve `u16*`, `u8*`, strides, número de planos ni módulos. Eso viaja dentro del *asset cocinado* y del *contexto de dispositivo*.
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

`screen.sprite` reemplaza hoy a `graphics::bob_draw(plan, bob, frame, x, y, target)`; el `BobTarget` (base, `row_bytes`, `plane_bytes`, planos, layout) se lo da el **contexto de dispositivo**, que lo construye desde la escena (`Scene::bitplanes()`), no el juego.

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

Reutiliza `scene::Camera2D` (`virtual_scene.hpp`) y `TileScrollDriver`/`FineScroll`; el `scroll_x` de la cámara es lo que hoy se parchea a mano en `BPLCON1` (en la 213, un `PatchHandle`).

### 2.1.4 Recursos — `app.load<T>(...)` y presupuesto

`load<T>` declara, carga y **cachea** un asset tipado, eligiendo Chip/Fast según el tipo (los datos que consume DMA —bitmaps, música P61, samples— van a Chip). El tipo `T` fija la decodificación (`Sprite`, `Music`, `Sample`, `Planes`), coherente con el `AssetCache` bytes-only + capa de decodificación separada (`RESOURCE_SYSTEM.md` §7).

```cpp
auto mod  = app.load<eng::Music>("assets/testmod.p61");   // -> AssetHandle<Music> / Result
auto spr  = app.load<eng::Sprite>("assets/abyss.bob");
sprite->draw(s, x, y);
app.resources().used_chip();                      // presupuesto consultable antes de pedir
```

`load<T>` es el sustituto del boilerplate actual (símbolo `incbin` + `allocate_block<Tag>` + `memcpy` + miembro por tag) y se apoya en `AssetCache` (`asset_cache.hpp`), el `Backend` de IO (`os::file_*`) y el presupuesto agregado (`HwInfo` + `LinearArena::remaining`). Devuelve handle/`Result`, no `Span<u8>` ni `Block<Tag>`.

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
| `graphics::bob_draw` + `BobTarget` | `Sprite` + `screen.sprite(...)` (§2.1.1) |
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
6. **`Sprite` + `screen.sprite(...)`** (§2.1.1): envolver `Bob`/`bob_draw` con un asset cocinado y un `BobTarget` que prepare el contexto de dispositivo; puerta visual `086_bob_objects`.
7. **`Palette` (`set`/`mix`/`fade`)** (§2.1.2): envolver `Palette32` + `util::palette_*` + `add_base_palette_patch`; gate host de la aritmética de color.
8. **`Camera`/`Layer` con `scroll_x`** (§2.1.3): unificar `Camera2D` + `TileScrollDriver`/`FineScroll` tras una capa con cámara.
9. **`app.load<T>(...)` + `resources()`** (§2.1.4): `Backend` Amiga para `AssetCache` + decodificación tipada + presupuesto agregado.
10. **Migrar una demo** al API completo (candidata: `204_collide_game` o la 086) como gate de cada abstracción.

Mientras tanto, el API de `eng/api/api.hpp` (fachada de tipos) sigue siendo la puerta de lo
existente; `App`/`Screen` lo envuelven para el caso de juego.

## 6. Referencias

- Principios y regla prioritaria: [PUBLIC_API.md](PUBLIC_API.md) §1.1.
- Contexto de dibujo y fuentes: `engine/include/eng/field/surface.hpp`.
- Revisión de estructura y decisiones: [ENGINE_STRUCTURE_REVIEW.md](ENGINE_STRUCTURE_REVIEW.md).
- UI (documentada): [GUI_LIBRARY.md](GUI_LIBRARY.md).
