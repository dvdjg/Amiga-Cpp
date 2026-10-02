# Roadmap de coherencia de la API del engine

Estado: **roadmap activo para limpiar la fachada de juego**. El diagnóstico se contrasta con
`eng/api/game.hpp`, `device.hpp`, `assets.hpp` y los consumidores reales; no asumir que el boceto
de este documento ya es el API implementado. Es la referencia para **no fijar dos verdades** al
construir lo que falta (planner de capas, decoders de assets, `World` completo). Complementa
los principios de [PUBLIC_API.md](PUBLIC_API.md) y el objetivo de
[PUBLIC_GAME_API.md](PUBLIC_GAME_API.md).

## 1. Propósito

El engine ha crecido por capas (core → field/graphics → scene → api) y la fachada de juego se
ha ido añadiendo sobre piezas internas. Este documento recoge **las incongruencias** verificadas
al usarla (ports de las demos 204/086/213/214), indica qué ya existe y ordena la limpieza pendiente
por fases con gates verificables. La arquitectura elegente de §§3.1–3.4 es objetivo, no descripción
del API que el juego consume hoy.

## 2. Diagnóstico (incongruencias)

Ordenadas por impacto sobre el diseño interno y la usabilidad de la API.

1. **Puertas e imports.** `eng/api/api.hpp` ya incluye `eng/api/game.hpp`; la puerta existe, pero
   reexporta varias cabeceras de bajo nivel y demos Amiga agregan backend/utilidades porque la fachada
   no cubre todos los casos.
2. **`App` es un *service locator*.** Expone a la vez alto nivel (`world`, `screen`, `input`,
   `audio`, `tasks`, `assets`) y hardware de bajo nivel (`memory()`, `scene()`, `copper()`,
   `copper_scheduler()`, `blitter_*`, `takeover_copper/commit_copper`). Es justo lo que
   `PUBLIC_API.md` §1.9/§11 quieren evitar. La fachada es también el cajón de servicios.
3. **Pila de dibujo solapada en cuatro capas.** `Screen` (≈ `DrawTarget` + `sprite`) →
   `DrawTarget` (fill/line/text/blit/c2p) → `Surface` (las mismas primitivas + `blit`) →
   `Rasterizer` (seam). No está claro cuál es *la* API de dibujo; el "contexto de dispositivo"
   de `PUBLIC_API.md` §12 está partido entre las cuatro.
4. **Dos modelos de propiedad en el subsistema de recursos.** `AssetCache` guarda `Ref<Backend>`
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
6. **Tipo y sistema de memoria requieren una separación de vidas útiles.** `MemoryKind
    {Chip,Slow,Fast,Any}` describe el banco; `MemoryManager`/`MemBank` describe las reservas de
    bancos y `MemorySystem::frame` describe scratch. La migración de ownership persistente no está
    cerrada: el backend Amiga aún usa `configure_backing` en parte de la configuración (MEM-001).
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

### 2.1 Estado contrastado con el API que compila

El API **no está todavía lo suficientemente limpio para ser la única interfaz de un developer**. Hay una fachada de juego útil para el camino habitual (`App`, `Screen`, `World`, `input`, `audio`, `assets`), pero se mezcla con un escape de máquina y varios módulos de objetivo aún no implementados. Estas son las fugas verificables que impiden marcarlo como API de nivel A terminado:

| Superficie actual | Problema observable | Consecuencia |
|---|---|---|
| `App::configure_memory(MemoryConfig)` y `App::device().memory_manager()` | El juego elige presupuestos por banco y puede reservar `Block<Tag>` manualmente. Lo usan, entre otras, las demos 011, 204, 213 y 086. | Arrancar un juego requiere conocer asignación y ciclo de memoria; el acceso bajo nivel no es un caso marginal. |
| `Device::blitter_clear`, `blitter_or_bobs`, `blitter_collide`, `execute_frame_plan`, `takeover_copper`, `commit_copper` | El consumidor pasa planos/dimensiones/strides/máscaras o planes de engine. | Es un escape útil para demos técnicas, pero no constituye una API de intención de juego simple. |
| `Screen::target()` | Devuelve `field::DrawTarget&`; `Screen::blit` pide dimensiones, stride, planos, shift y operación. | `Screen` aún permite saltar directamente al modelo de raster/plan; la frontera de nivel A no está cerrada. |
| `Assets::add_bitmap`/`bitmap` + `Screen::bitmap` | La copia de assets planares ya conserva geometría/layout y encola la copia a la escena; HOST-234 comprueba datos y demo 213 muestra el fondo en WinUAE. | Queda una llamada de dominio usable, pero el acceso a layout en `GameDisplay`, `bind_scene` y configuración manual de memoria siguen exponiendo composición. |
| `App::load(path, size, MemoryRequest, priority)` | Carga bytes por tamaño conocido y devuelve un `AssetId`; `asset_view()` devuelve la vista no propietaria. No existe el decoder tipado `load<Sprite>(path)` descrito en el objetivo. | El juego conoce detalles de almacenamiento y tiene que resolver formato/tipo/vida útil. |
| `World::emit` / `App::draw_world()` | Requieren `BobTarget`, `DirtyRect` y `FramePlan`; las capas de tilemap/fondo todavía no se materializan desde `World`. | El modelo retenido no es aún el camino de render completo; la demo 086 conserva planificación de máquina explícita. |
| Resultados de operaciones | Coexisten `bool`, id cero, vista vacía y bloques inválidos. | La llamada requiere convenciones locales para distinguir fallo de recurso pendiente o API ausente. |

Conclusión: la API es **parcialmente limpia**, no final. Su superficie actual es razonable para prototipos y demos que aceptan el nivel `Device`; aún no permite a un developer hacer un juego 2D completo sin configurar memoria, reservar buffers o entender handles crudos. El diseño de `GAME_API_TWO_LEVELS.md` es objetivo, no prueba de que esa capa A ya exista.

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
| `Screen`/`DrawTarget`/`Surface`/`Rasterizer` | **Objetivo**: `Screen` = contexto normal; actual `Screen::target()` aún devuelve `DrawTarget&` |
| `eng::MusicModule` + `eng::audio::MusicModule` | un `MusicBytes`/`Music` |
| `Sprite` (objeto) + sprite hardware | `Sprite` (objeto) y `HwSprite` (representación) |
| `SceneLayout` + `BobLayout` | un `PlaneLayout` |
| `MemoryKind` mezclado con vida útil (`frame`) y banco efectivo | `MemoryKind` para banco, `MemoryManager` para reservas y `ScratchArena` para vida temporal; migración persistente abierta en MEM-001 |
| `bool`/`0`/bloque inválido | `Expected<T>` (valor o `eng::Result`) + `[[nodiscard]]` |
| `takeover`/`commit`/`present` | nombres actuales quedan por módulo; unificar solo al migrar consumidores reales |

## 4. Roadmap por fases

Cada fase es **autocontenida, verificable y revertible**. Las demos y los tests host son el
gate; ninguna fase rompe una demo verde sin migrarla en la misma pasada.

### F0 — Guardas y acuerdo
- Roadmap enlazado; `api-facade.mjs` prohíbe acceso directo al backend desde demos/juegos.
- No hay check que limite los miembros públicos de `App`/`Device`; conviven servicios de juego y
  rutas de bajo nivel.
- Estado: parcial. Antes de sumar métodos públicos, decidir si es API de juego o escape técnico.

### F1 — Puerta única y dominio (bajo riesgo)
- `api.hpp` ya incluye `game.hpp`; todavía reexporta cabeceras internas de `field`, `graphics`,
  `scene` y otros módulos.
- Unificar nombres: `Music` (un tipo) vs `HwSprite*` (**hecho**), `PlaneLayout` (**hecho** como alias único).
- `Result<T>` existe y se usa en APIs nuevas, pero `bool`, id cero, vistas vacías y bloques inválidos
  siguen formando parte del flujo público. Estado: parcial.

### F2 — Separar `App` en composition root + `Device`
- `App` se queda con: bucle, `screen`, `world`, `input`, `audio`, `tasks`, `assets`, `port`.
- `Device` agrupa `memory`/`blitter_*`/`copper`/`raster`/`presupuesto`; se accede por
  `app.device()` y es **ignorable** por juegos simples.
- **Parcial**: existe `app.device()` + demos 204/086/209/214 migradas; el check `api-facade.mjs`
  prohíbe en demos/juegos el hardware directo de `App` (`app.memory/blitter_*/copper/scene/…`).
- `App` todavía expone `configure_memory`, `memory_manager`, `load_asset`, ids/vistas/leases crudas,
  `draw_world`, `takeover` y `present`; `Screen::target()` filtra `DrawTarget`. No marcar F2 como
  limpieza concluida mientras esos accesos sean la forma requerida por consumidores representativos.
- Eliminar duplicados de `App` solo después de migrar los consumidores; reservar `Device` para el
  escape deliberado de demos técnicas, con operaciones basadas en intención donde exista reemplazo.
- Estado: parcial. Es útil como separación de servicios para las demos migradas, pero no cumple aún
  la frontera de API de juego descrita en `PUBLIC_API.md`.
- *Gate*: check de F0 pasa; demos migradas verdes.

### F3 — Unificar y estrechar el contexto de dibujo (`Screen`) — pendiente
- `Screen` debe ser la superficie de uso común: `clear`, primitivas, texto, sprite y blit de imagen
  cocinada. Actualmente `Screen::blit` pide `src_row_bytes`, `src_plane_stride`, `planes`,
  `source_shift`, dirección descendente y `RasterOp`, mientras `Screen::target()` devuelve
  `field::DrawTarget&`. Eso es válido como escape de transición, no como fachada terminada.
- Mantener un escape interno/local para técnicas que realmente necesiten `DrawTarget`/`FramePlan`; no
  fingir que `Screen` ya los oculta al 100 % ni crear una segunda capa de wrappers redundantes.
- Gate: adaptar un consumidor de juego representativo al `Screen` normal, dejar el escape técnico
  acotado a demos de técnica y probar equivalencia del render.

### F4 — `World` de alto nivel + planner
- Parcial: `World`/`Layer`/cámara y `draw_world()` para actores existen, pero el juego todavía liga una
  `Scene` manual y `App::draw_world()` construye `BobTarget`/`DirtyRect`/`FramePlan` internamente.
- Falta materializar capas de bitmap/tilemap y dar a la app un `World::present(Screen&)` que no exponga
  plumbing de actor/plan. La demo 086 sigue creando su propio `copper::Plan`/FramePlan mediante
  `app.device()` por ser una demo técnica; no se exige migrarla al nivel A hasta que el planner cubra
  su caso.
- Gate: juego sencillo con fondo/capa + actor que solo use `App`, `World` y `Screen`, sin `bind_scene`
  ni `BobTarget`/`FramePlan`.

### F5 — Assets tipados y error único
- Parcial: `Assets::add<Tag>` copia blobs preconocidos; `AssetRuntime` carga por path y tamaño y
  devuelve IDs/vistas; `App::load<T>` no es el decoder tipado del ejemplo. `Assets::create<Tag>`
  devuelve un `Block<Tag>` y el registro/fase tiene reglas de liberación separadas.
- Objetivo: un handle tipado por asset y un flujo de error único. La carga async debe ser explícita si
  el juego la necesita, sin filtrar bancos, tamaños físicos ni punteros. No cerrar ownership completo
  como prerrequisito de la limpieza general de la fachada.
- Gate: demo que carga un sprite tipado, recibe ready/error y lo usa por `Screen::sprite`; HOST cubre
  formato, generación e invalidación del handle.

### F6 — Limpieza
- Retirar o acotar accesos duplicados solo después de migrar los consumidores reales; no esconder
  `Device` de las demos técnicas ni cambiar la fachada por estética.
- Actualizar el índice y la matriz de demos; gate `api-facade`, docs y pruebas del consumidor nuevo.

**Lectura del estado actual:** F1–F6 no están cerradas como bloque. F2 existe como separación de
servicios; F3–F5 tienen piezas aisladas, pero falta el camino de capas y assets del ejemplo de juego.
No marcar la fachada como «API limpio final» hasta que un juego representativo complete
init/update/render con vocabulario de dominio y los escapes de `Device` queden acotados a demos
técnicas.

### Orden de salida para la próxima implementación de API

1. **Hecho (gate inicial)**: HOST-234 incluye solo `<eng/api/api.hpp>` y valida `App`/`Screen`; la
   demo 214 compila y ejecuta el consumer con esa misma puerta única. Esto fija el contrato mínimo,
   aunque el arranque de escena y el planner de capas sigan siendo explícitos.
2. Resolver arranque/liga de escena con presets de juego, dejando `configure_memory`, `Scene` y
   `scene::compose` dentro del composition root/backend.
3. Retirar `Screen::target()` de la superficie normal; no reemplazarlo por wrappers uno-a-uno.
4. Materializar un fondo/capa real en `World` y presentar un `Sprite` por asset de dominio.
5. Mantener `Device`/API tipada interna para demos técnicas; no filtrarla a la firma del juego.

## 5. Criterios de aceptación

- **Una puerta**: un juego compila solo con `<eng/api/api.hpp>`.
- **Sin plumbing de hardware en un consumer nivel A**: un ejemplo de juego escrito con
  `App`/`World`/`Screen` no necesita `copper::`, `BPLCON`, `DMACON`, `BobTarget`, `FramePlan` ni
  configuración manual de `Device`. Las demos bajo `techniques/` quedan excluidas porque su objetivo
  es enseñar la técnica.
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
`eng::BlockPool` (bloques fijos con `free`, sobre `core/util/pool.hpp`) — **general**, útil a
cualquier juego/streaming.

### 7.3 Fase F7 — helpers generales (no específicos de NES)

Todo lo de abajo es **reutilizable** por cualquier juego/emulador; el adaptador NES se apoya en
ello. Ordenados por dependencia:

1. **`decode_2bpp_planar`** (`eng::graphics`): decode de tiles indexados → planos; cubre
   `IPatternCache::define` y `ISpriteEngine::define`. **Hecho** (`eng/graphics/tile_planar.hpp`,
   HOST-338).
2. **Tile layer de juego**: `set_tile`/`set_attribute`/`flush(dirty)` sobre `TileLayer` +
   `TileScrollDriver`; cubre `IScrollingLayer`. **Hecho (modelo)**: `tilemap::TileEditor`
   (HOST-342) + `tilemap::AttributeTable` (HOST-344); falta el **driver** que materializa lo
   sucio (ligado a F7.3). Gate en 100/052.
3. **Drivers de scroll por `ScrollKind`**: `XLimited` (`CopperRing`) y `BlitterColumns`/`Fine`
   **ya existen** (`field::TileScrollDriver`/`xlimited_*`, `effects::FineScroll`); falta el driver
   **`CopperSplit` (`XYUnlimited`)** (ring + split por línea + guardas). **Núcleo de decisión
   hecho** (`scroll_plan.hpp`: `choose_scroll`/`scroll_memory`/`plan_region`, HOST-343/345).
   Gate: demo de scroll 8-way.
4. **Fachada de Copper** en `Device`/`Screen` (`begin`/`wait_line`/`set_color`/`set_scroll`/
   `split`/`commit`/`free_words`): cubre `ICopper`. **Hecho** (`eng::Copper`,
   `Device::copper_builder()`, HOST-339).
5. **Voz Paula**: **cubierto** por `AudioMixer::play(AudioPlan::Channel{period,...})` +
   `paula::period_for_hz` (`eng/audio/audio.hpp`, `audio_mode.hpp`); el consumidor mapea
   `IAudio::play/set_period` a un `SampleEvent` sin código nuevo.
6. **`eng::BlockPool`** (bloques fijos con `free`) + `res::Budget`: cubre `IChipMem` reutilizable.
   **Hecho** (`eng/memory/block_pool.hpp`, HOST-340).
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
Demostrado por **HOST-341** (adaptadores `IChipMem`→`BlockPool`, `ICopper`→`Copper`).
