# Roadmap de coherencia de la API del engine

Estado: **propuesta** (diagnóstico + plan). Es la referencia para **no fijar dos verdades** al
construir lo que falta (planner de capas, decoders de assets, `World` completo). Complementa
los principios de [PUBLIC_API.md](PUBLIC_API.md) y el objetivo de
[PUBLIC_GAME_API.md](PUBLIC_GAME_API.md).

## 1. Propósito

El engine ha crecido por capas (core → field/graphics → scene → api) y la fachada de juego se
ha ido añadiendo sobre piezas internas. Este documento recoge **las incongruencias** que han
aparecido al usarla (port de las demos 204/086/213/214), propone **una arquitectura objetivo
elegante** y ordena el trabajo en **fases** con gates verificables.

## 2. Diagnóstico (incongruencias)

Ordenadas por impacto sobre el diseño interno y la usabilidad de la API.

1. **Dos puertas de entrada.** `eng/api/api.hpp` (fachada de tipos) **no** incluye
   `eng/api/game.hpp` (`App`/`Screen`), así que el include "oficial" no da el bucle de juego, y
   quien quiere `App` incluye otra cabecera. Debe haber **una sola** puerta.
2. **`App` es un *service locator*.** Expone a la vez alto nivel (`world`, `screen`, `input`,
   `audio`, `tasks`, `assets`) y hardware de bajo nivel (`memory()`, `scene()`, `copper()`,
   `copper_scheduler()`, `blitter_*`, `takeover_copper/commit_copper`). Es justo lo que
   `PUBLIC_API.md` §1.9/§11 quieren evitar. La fachada es también el cajón de servicios.
3. **Pila de dibujo solapada en cuatro capas.** `Screen` (≈ `DrawTarget` + `sprite`) →
   `DrawTarget` (fill/line/text/blit/c2p) → `Surface` (las mismas primitivas + `blit`) →
   `Rasterizer` (seam). No está claro cuál es *la* API de dibujo; el "contexto de dispositivo"
   de `PUBLIC_API.md` §12 está partido entre las cuatro.
4. **Dos modelos de propiedad para el mismo par.** `AssetCache` guarda `Ref<Backend>`
   (**observa**, `asset_cache.hpp`), mientras `AssetRuntime` **posee** su `CacheBackend`
   (`asset_runtime.hpp`). Además `res::load<Tag>` devuelve `Block<Tag>` (dueño) y
   `AssetRuntime::bytes<Tag>` devuelve `ByteView<Tag>` (vista): dos "handles" con semántica
   distinta.
5. **Nombres de dominio duplicados/sobrecargados.** `eng::MusicModule` (`domains.hpp`,
   `ByteView<MusicTag>`) y `eng::audio::MusicModule` (`music_player.hpp`, struct) — **resuelto**
   renombrando el alias a `MusicBytes`; `Sprite` (objeto BOB en `sprite_asset.hpp`) y el sprite
   hardware (`sprite.hpp`, `HwSpriteTemplate`/`HwSpritePlacement`) — **resuelto** con el prefijo
   `Hw*`; `SceneLayout`/`BobLayout` — **resuelto**: unificados como alias de
   `eng::graphics::PlaneLayout` (y `eng::gfx::PlaneLayout` ya aliasa al mismo; `Separate = Contiguous`).
6. **Tipo y sistema de memoria no cuadran.** `MemoryKind {Chip,Slow,Fast,Any}` pero
   `MemorySystem {chip,slow,frame}`: **no hay arena Fast** (se mapea `Fast → slow`) y **no hay
   `MemoryKind::Frame`**. Tipo y almacenamiento se contradicen.
7. **Error *ad-hoc*.** `eng::Result` existe pero apenas se usa: las APIs fallan con `bool`, `0`,
   bloque inválido o `Ref` nulo. Falta un idioma único de error.
8. **Verbos sobrecargados.** `takeover`/`commit`/`present`/`flip_copper` en `Scene`,
   `copper::Plan` y `App` con matices distintos (instalar vs parchear vs publicar vs voltear).
9. **`World` filtra el bajo nivel.** `World::emit(plan, Span<const BobTarget>, DirtyRect, cam…)`
   y `App::draw_world()` manejan `BobTarget`/`FramePlan`; el mundo retenido no debería conocerlos.
10. **Entrada con dos caminos.** `App::input()` (`InputAggregator`, sondeo) y los mensajes del
    mini-SO (`Joystick`/`Gamepad`/`MouseButton`). Transitorio, pero hoy hay dos verdades.
11. **`api.hpp` mezcla niveles.** Reexporta tipos crudos de `core`, `field`, `graphics`,
    `scene`, `ui`, `task`… sin separar "lo que usa un juego" de "lo que usa el engine".

## 3. Solución elegante (arquitectura objetivo)

La idea rectora: **una puerta, tres niveles, nombres de dominio y fallo explícito**. El juego
describe **intención**; el engine decide **materialización**; el hardware no se nombra.

### 3.1 Principios

- **Una sola puerta y un solo idioma.** `#include <eng/api/api.hpp>` da `App` y todo lo que el
  juego necesita. No se documentan dos formas de hacer lo mismo.
- **Descubrimiento por niveles.** `app.` → subsistemas de juego; `app.device().` → servicios de
  hardware; `eng::graphics::…` → **interno** (nunca en código de juego).
- **Un concepto, un nombre.** Un único `Sprite`, un único tipo de música, un único enum de
  layout.
- **Fallo explícito.** `[[nodiscard]]` + un `Expected<T>` común (valor o `eng::Result`); nunca
  `0`, ni bloque inválido, ni `Ref` nulo como error silencioso.
- **Sin registros, punteros ni offsets** en la frontera (ya en `PUBLIC_API.md` §12); lo
  específico de hardware vive en el driver/`Device`.

### 3.2 Capas

```text
  nivel Juego        app.screen() · app.world() · app.input() · app.audio()
                     app.assets() · app.tasks() · app.device()
                     (describe QUÉ; no ve planos, copper ni blitter)
        │
  nivel Dispositivo  Screen (contexto de dibujo único) · Device (servicios:
                     blitter/copper/raster/presupuesto) · World (capas+actores+cámaras)
                     (media ENTRE intención y hardware; tipos de dominio)
        │
  nivel Interno      composition::Scene · copper::Plan/Scheduler · FramePlan ·
                     Rasterizer · Surface · Bob · arena/Block · backend Amiga
                     (NO se reexporta al juego)
```

### 3.3 Frontera pública (qué se expone)

- **Juego**: `App`, `Screen`, `World`/`Layer`, `Sprite`, `Palette`/`Color`, `Input`,
  `Audio`, `Assets`, `Task`, `Box`, `Color`, `Expected<T>`/`Result`.
- **Dispositivo**: `Device` (servicios), `Screen`. El juego lo usa cuando necesita Blitter/
  copper, pero **por intención** (`device.fill_rect(...)`, `device.or_bobs(...)`,
  `device.install_copper(...)`), no por registros.
- **Interno (no público)**: todo `eng/graphics/*` de bajo nivel, `copper::*`, `field::*`,
  `scene::ActorEmitContext`, `memory/arena`, `Block`, `BobTarget`.

### 3.4 API objetivo (boceto)

```cpp
#include <eng/api/api.hpp>          // única puerta

struct MiJuego {
    eng::Sprite nave;               // asset cocinado (objeto)
    void init(eng::App& app) {
        auto fondo = app.world().add_layer("fondo", 0);
        fondo->camera().set_scroll_x(0);
        app.assets().load<eng::Music>("audio/tema.p61");   // Result<Asset<Music>>
    }
    void update(eng::App& app) { /* app.input(), app.world()... */ }
    void render(eng::App& app) {
        auto s = app.screen();
        s.clear(0);
        s.sprite(nave, x, y);                 // contexto de dibujo único
        app.world().present(s);               // capas + actores (sin BobTarget)
    }
};

eng::amiga::AmigaBackend backend {};
MiJuego juego {};
eng::App app {backend, juego};               // composition root
app.run();
```

### 3.5 Nombres y errores (decisiones)

| Hoy (incongruente) | Objetivo |
|---|---|
| `api.hpp` y `game.hpp` separados | `api.hpp` reexporta `App`/`Screen`/`World` |
| `App::memory()/copper()/scene()/blitter_*` | `App::device()` agrupa servicios; `App` queda corto |
| `Screen`/`DrawTarget`/`Surface`/`Rasterizer` | **`Screen`** = contexto de dispositivo; el resto, interno |
| `eng::MusicModule` + `eng::audio::MusicModule` | un `MusicBytes`/`Music` |
| `Sprite` (objeto) + sprite hardware | `Sprite` (objeto) y `HwSprite` (representación) |
| `SceneLayout` + `BobLayout` | un `PlaneLayout` |
| `MemoryKind{Fast}` sin arena / `frame` sin kind | `MemoryKind` y `MemorySystem` alineados |
| `bool`/`0`/bloque inválido | `Expected<T>` (valor o `eng::Result`) + `[[nodiscard]]` |
| `takeover`/`commit`/`present` | `install`/`publish` (vocabulario único) |

## 4. Roadmap por fases

Cada fase es **autocontenida, verificable y revertible**. Las demos y los tests host son el
gate; ninguna fase rompe una demo verde sin migrarla en la misma pasada.

### F0 — Guardas y acuerdo
- Documentar este roadmap y enlazarlo (`README` de arquitectura, `DOC-MAP`).
- **Congelar** superficie nueva de juego hasta F1–F2 (evitar más `App::*` de bajo nivel).
- Añadir un check ligero: `App` no puede exponer `memory()`/`copper()`/`scene()` tras F2
  (lista blanca en un `.mjs`).
- *Gate*: `node tools/check/*` verde.

### F1 — Puerta única y dominio (bajo riesgo)
- `api.hpp` incluye `game.hpp` y deja de reexportar cabeceras internas que el juego no usa.
- Unificar nombres: `Music` (un tipo) vs `HwSprite*` (**hecho**), `PlaneLayout` (**hecho** como alias único).
- Introducir `Result<T>`/`Status` y usarlo en las APIs **nuevas**.
- *Gate*: todas las demos compilan/run igual; HOST en verde.

### F2 — Separar `App` en composition root + `Device`
- `App` se queda con: bucle, `screen`, `world`, `input`, `audio`, `tasks`, `assets`, `port`.
- `Device` agrupa `memory`/`blitter_*`/`copper`/`raster`/`presupuesto`; se accede por
  `app.device()` y es **ignorable** por juegos simples.
- **Hecho**: `app.device()` + demos 204/086/209/214 migradas; el check `api-facade.mjs`
  prohíbe en demos/juegos el hardware directo de `App` (`app.memory/blitter_*/copper/scene/…`).
- Retirar los métodos directos de `App` (delegados a `Device`) es limpieza mecánica (F2b).
- *Gate*: check de F0 pasa; demos migradas verdes.

### F3 — Unificar el dibujo en `Screen`
- `Screen` es **la** API de dibujo (primitivas + `sprite` + `present` del plan).
- `Surface`/`DrawTarget`/`Rasterizer` pasan a internos (no reexportados).
- *Gate*: HOST-234 (App/Screen) + demos de dibujo verdes.

### F4 — `World` de alto nivel + planner
- `Layer` con **contenido** (bitmap planar; `TileLayer` aparte) y cámara.
- `World::present(Screen&)` emite capas + actores (sin `BobTarget`/`FramePlan` en la firma).
- Migrar `086` a `world.add_actor` + `World::present`.
- *Gate*: demo de scroll por capas + 086.

### F5 — Assets tipados y error único
- `Assets::load<T>(path)` con `Result<Asset<T>>`; decoders con receta
  (`SpriteRecipe`/`PlaneSpec`/`MusicCodec`).
- `AssetCache` y `AssetRuntime` con **un** modo de propiedad (la caché no guarda `Ref` al
  backend; el runtime es el dueño).
- *Gate*: demo de carga asíncrona con assets de disco + HOST.

### F6 — Limpieza
- Eliminar lo deprecado (`api.hpp` viejo, dobles nombres), migrar demos restantes.
- Cerrar el rojo ajeno (`header-impl` de `goap.hpp`) y dejar `tools/run-host-tests.sh` verde.
- *Gate*: regresión completa.

## 5. Criterios de aceptación

- **Una puerta**: un juego compila solo con `<eng/api/api.hpp>`.
- **Sin hardware en juego**: `grep` sobre `demos/games` no encuentra `copper::`, `BPLCON`,
  `DMACON`, `BobTarget`, `FramePlan`, `Blitter`.
- **Un nombre por concepto**: sin `MusicModule` duplicado ni `Sprite` ambiguo.
- **Fallo explícito**: las APIs nuevas devuelven `Expected<T>`/`Result` con `[[nodiscard]]`.
- **Memoria coherente**: `MemoryKind` ⇔ `MemorySystem` (sin mapeos implícitos).
- **Demos y HOST verdes** en cada fase.

## 6. Referencias

- Principios y frontera: [PUBLIC_API.md](PUBLIC_API.md) (§1.1, §11, §12).
- Objetivo de juego: [PUBLIC_GAME_API.md](PUBLIC_GAME_API.md).
- Modelo retenido y recursos: [SCENE_AND_RESOURCES.md](SCENE_AND_RESOURCES.md).
- Consolidación y decisiones: [ENGINE_STRUCTURE_REVIEW.md](ENGINE_STRUCTURE_REVIEW.md).
- Estilo y restricciones: [CODING_STYLE.md](CODING_STYLE.md).

## 7. Adaptaciones para consumidores externos (emulador NES)

Un consumidor externo (p. ej. el emulador NES → Amiga 500, `RetroReverse`) define sus propias
interfaces `I*` (vocabulario **suyo**: `IChipMem`, `IBlitter`, `IScrollingLayer`, `ICopper`,
`ISpriteEngine`, …). Regla: **las `I*` son externas**; el engine **no** las adopta. El engine da
su **API de dominio** + unos *helpers generales*, y el consumidor escribe un **adaptador fino**.
Las `I*` deben ser **implementables o, al menos, equivalentes** con lo que el engine ofrece.
La ficha del consumidor NES (opciones de implementación, índice de la referencia y **decisión de
scroll**) vive en [NES_CONSUMER.md](../NES_CONSUMER.md).

### 7.1 Principio

> «La app pide; el engine dispone.» El consumidor expresa **intención** (mover el fondo, cambiar un
> color en una línea, colocar N sprites); el engine decide el *cómo* y **mantiene la propiedad de
> los recursos** (`Scene`/`World` poseen bitplanes/capas; `res` posee los assets). El adaptador solo
> traduce intención externa → API del engine (no toca planos, Copper ni Blitter).

### 7.2 Sobre `IChipMem`

No hace falta exponer un asignador crudo: si el consumidor necesita **un recurso concreto**
(buffer de audio, superficie de dibujo, nametable), el engine **se lo entrega como objeto** de su
sistema (`Scene`/`Layer`/`AudioSystem`), y la *cuota* se consulta con `res::Budget`
(`remaining_chip`/`can_fit`). El `alloc/free` por bloque de `IChipMem` no encaja con la arena *bump*
del engine; si de verdad hace falta memoria **reutilizable** (nametables/CHR que cambian), se añade
`res::ChipPool` (bloques fijos con `free`, sobre `core/util/pool.hpp`) — **general**, útil a
cualquier juego/streaming.

### 7.3 Fase F7 — helpers generales (no específicos de NES)

Todo lo de abajo es **reutilizable** por cualquier juego/emulador; el adaptador NES se apoya en
ello. Ordenados por dependencia:

1. **`chr_to_planar`** (`eng::graphics`): decode 2bpp (y variantes) → planar; cubre
   `IPatternCache::define` y `ISpriteEngine::define`. **Hecho** (`eng/graphics/chr.hpp`,
   HOST-338).
2. **Tile layer de juego**: `set_tile`/`set_attribute`/`flush(dirty)` sobre `TileLayer` +
   `TileScrollDriver`; cubre `IScrollingLayer`. **Hecho (modelo)**: `tilemap::TileEditor`
   (HOST-342) + `tilemap::AttributeTable` (HOST-344); falta el **driver** que materializa lo
   sucio (ligado a F7.3). Gate en 100/052.
3. **Drivers de scroll por `ScrollKind`**: `XLimited` (existe) + `XYUnlimited`/`CopperSplit` y
   `BlitterColumns` (robocod), con `region_cost` reservando bandas en `copper::Plan`.
   **Núcleo hecho** (`eng/scene/scroll_plan.hpp`: `choose_scroll`/`scroll_memory`,
   HOST-343); faltan los **drivers** (materializar ring/split). Gate: demo de scroll 8-way.
4. **Fachada de Copper** en `Device`/`Screen` (`begin`/`wait_line`/`set_color`/`set_scroll`/
   `split`/`commit`/`free_words`): cubre `ICopper`. **Hecho** (`eng::Copper`,
   `Device::copper_builder()`, HOST-339).
5. **Voz Paula**: **cubierto** por `AudioMixer::play(AudioPlan::Channel{period,...})` +
   `paula::period_for_hz` (`eng/audio/audio.hpp`, `audio_mode.hpp`); el consumidor mapea
   `IAudio::play/set_period` a un `SampleEvent` sin código nuevo.
6. **`res::ChipPool`** (bloques fijos con `free`) + `res::Budget`: cubre `IChipMem` reutilizable.
   **Hecho** (`eng/res/chip_pool.hpp`, HOST-340).
7. **`SpriteEngine` a alto nivel** (`begin_frame`/`place`/`draw_bobs`/`add_to_copper`): cubre
   `ISpriteEngine` con semántica NES (8 sprites/línea, overflow → descartar). **Cubierto** por
   `scene::compose_sprites` + `build_sprite_intents` + `emit_bob_fallbacks` + `actor_add_copper`
   sobre `ActorStore`/`SpriteAllocator` (el consumidor mapea `place` a `add_actor`).

Adaptadores triviales (ya cubiertos por el engine): `IBlitter`→`Device`/`FramePlan`;
`IPalette`→`Palette`/`FramePlan`; `IInput`→`App::input()`; `IDisplay`→`c2p`+`present`; `ISurface`→
`gfx::Bitmap`.

### 7.4 XYUnlimited y DPF para el caso NES

El NES necesita scroll **en ambos ejes** con wrap: se implementa con `ScrollKind::CopperSplit`
(*xyunlimited*, split por línea) — una **por banda** (coste Copper) — no con `XLimited`
(solo útil en horizontal). El BG NES se materializa como `Layer{Tilemap, CopperSplit, Pf2}` en un
`SceneMode::DualPlayfield`, con `WorldRegion` para la status bar (banda superior). La capa **pide**;
el planner (F4c) **dispone/degrada**.

### 7.5 Gate

El propio **emulador NES** (DPF + BG `XYUnlimited` + sprites por `ActorStore` + audio Paula) como
consumidor de referencia: si las `I*` se implementan sin tocar planos/Copper/Blitter, la frontera
es correcta.

### 7.6 Decisión: ¿las `I*` en el engine o fuera?

**Recomendación: las `I*` viven FUERA del engine.** Razones:
- Son **vocabulario de un consumidor** concreto; meterlas en `engine/` acopla el engine a una app
  y multiplica superficies públicas (una por consumidor).
- Arrastran **virtuals/ABI** (o punteros a función + `void* self`), que el engine evita en el hot
  path y en sus cabeceras (`CODING_STYLE.md`).
- El consumidor **debe seguir siendo libre** de elegir C++ virtual, ABI C o adaptadores `static`.

La frontera correcta son los **helpers generales** (F7.1–F7.7) + `eng/api/api.hpp`. El consumidor
define sus `I*` en **su proyecto** e implementa un **adaptador fino** sobre el engine (manteniendo
la propiedad de recursos en el engine). Si conviene, el engine puede incluir un **adaptador de
referencia** fuera del core (`host-tools/` o `examples/`, no `engine/`) que demuestre la frontera.

**Prueba de la decisión**: implementar las `I*` **solo** con `<eng/api/api.hpp>` y los helpers,
sin `#include` de `eng/graphics/copper/*`, `eng/field/*`, `BobTarget`, `FramePlan` ni planos. Si
compila y no filtra registros, la separación es correcta y **el engine no se ve afectado**.
Demostrado por **HOST-341** (adaptadores `IChipMem`→`ChipPool`, `ICopper`→`Copper`).
