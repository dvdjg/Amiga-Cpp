# Roadmap de la fachada de juego (API de juego limpio)

Estado: **en curso**. §1 (arranque cero-config) y §3 (ocultar composición/escena) **hechos para la
213** vía `App::start()` + `GameDisplay` declarativo (la 213 ya no escribe `configure_memory`,
`compose`, `SceneResources`, `ocs_a500`, `BPLCON0`, `takeover`; 49,9 fps). §5 con `Anim` + colisión
de caja. §2 (audio auto-conducido: formato + buffer P61 + `App::play_music`) y §4 (el `INCBIN` sale
del juego a un manifiesto) con su base hecha. §6 (estados de escena) **base hecha**: pila de escenas
en la fachada (`push_scene`/`pop_scene`/`set_scene`, HOST-240). **Pendientes**: §4 (generador del
manifiesto), §7 (cámara/tilemap de juego), §8 (vocabulario), §9 (plantilla + 213 sin una línea
técnica; base en `games/000_template`). El contrato de §0 («juego de 30 líneas») **aún no compila**
del todo.
Origen: revisión honesta de lo que la demo 213 necesita escribir (ver
[`GAME_API_TWO_LEVELS.md`](../../engine/architecture/GAME_API_TWO_LEVELS.md)). El objetivo de este roadmap es que una
persona pueda **cerrar un juego 2D sin bajar al metal**: la "capa A" debe bastar.

## 0. Criterio de éxito: "tu primer juego en 30 líneas"

El contrato contra el que se juzga cada paso es este (hoy **no** compila; es el destino):

```cpp
#include <eng/game.hpp>   // una cabecera: App + Screen + assets + audio + input

struct MyGame {
    void init(eng::App& app) {
        m_hero = app.assets().sprite("hero");          // formato/Chip/alineación: cosa del engine
        app.audio().play_music("level1");              // registra su frame task y arma el DMA solo
        app.input().enable();                          // pad0/teclado
    }
    void update(eng::App& app) {
        if (app.input().left())  m_x -= 2;
        if (app.input().right()) m_x += 2;
        if (app.input().fire())  app.audio().play_sfx("boom");
        m_anim.update();
    }
    void render(eng::App& app) {
        app.screen().cls(eng::Colors::Blue);
        app.screen().text(8, 8, "SCORE 000");
        app.screen().sprite(m_hero, m_anim.frame(), m_x, m_y);
    }
};
ENG_GAME_MAIN(MyGame);
```

Todo lo que hoy **no** sea esto es trabajo de este roadmap: `configure_memory`, `res::load`, `Block<Tag, Chip>`,
`INCBIN`+memcpy, `0x5200`, `os::set_frame_task`, `p61_needs_sample_buffer`, `bob.sheet/width/...`, `#if ENG_AMIGA`.

**Premisa de coste** (canónica en [`CODING_STYLE.md`](../../engine/architecture/CODING_STYLE.md)): el API de juego es
de **coste ~cero en el bucle de animación** — la fachada no añade ciclos por frame; el **setup** (init, carga,
composición) puede ser lento; lo resoluble en compilación se resuelve con C++23 (`consteval`/`constexpr`,
`if constexpr`, `concept`, parámetros `constexpr`).

## 1. Arranque cero-config

- **Problema**: el juego llama `app.configure_memory({96k, 8k, 4k, 0})` y `comp::compose(...)` con `ocs_a500`,
  `SceneResources` y un `BPLCON0` crudo (`comp::display(res, 0x5200)` en 213).
- **Salida**: `App::start()` compone un display declarativo dentro del pool de memoria asignado por el
  composition root. El juego no nombra `SceneResources`, `ocs_a500` ni registros. Perfiles de producto
  A500/A1200 son valores por defecto; el composition root puede suministrar un `MemoryConfig` propio.
- **Política de RAM acordada**: asignar pools de juego desde presupuestos populares conocidos (A500:
  512 KiB Chip + 512 KiB Slow; A1200: 2 MiB Chip), dejando headroom explícito a Exec. Fast RAM no se
  presupone: un integrador que la detecte puede añadirla mediante el perfil personalizado. `AvailMem`
  solo consulta el mayor bloque contiguo antes de `AllocMem`; no reserva memoria ni garantiza que el
  bloque siga libre. `configure_game_memory()` selecciona perfil desde `HwInfo` y limita la reserva
  automática al bloque libre preservando headroom. Demo 214 ejercita selección automática y pasa
  `build -> run -> analyze` en WinUAE/A500. `App::start()` y `GameDisplay` componen y poseen la escena
  sobre un `MemoryManager` preconfigurado por el composition root; HOST-234 cubre éxito, errores tipados,
  reintento, `run(n)` finito y liberación de la escena. L1-001 ejecuta el ciclo completo en WinUAE y
  exige `DMACONR=0` más recuperación del pool Chip tras destruir `App`. HOST-394 valida selección
  automática y preflight puro. AGA/A1200 tiene cobertura host; el runner no ofrece un perfil AGA.

## 1.1 Materializador inicial de capas `World`

- **Alcance implementado**: `App::add_background(id, depth, bounds, color)` añade una capa opaca
  `WorldLayerKind::Fill`; `App` materializa esas regiones antes de `Game::render`, trasladándolas con
  la cámara y recortándolas al viewport. Profundidad ascendente con orden estable: el fondo se compone
  primero y los objetos del juego después.
- **Gate**: HOST-234 verifica cámara, recorte, profundidad y píxeles. Demo 214 usa `World` para limpiar
  el fondo móvil detrás de ambos BOBs y pasa el gate visual de WinUAE.
- **Límites de este hito**: no materializa tilemaps ni bitmap assets y no deduce DPF/planos de varias
  capas. Es un materializador CPU de regiones opacas, no el planner general de escenas.

## 2. Audio auto-conducido (el eslabón con más dolor)

- **Problema**: la música **no se conduce sola**. La 213 hace `app.memory_manager().chip().reserve<AudioTag>(need, 4)`,
  `p61_needs_sample_buffer`, `os::set_frame_task(&music_tick, this)` y (por el bug del playroutine) necesita que el
  engine re-arme el DMA. Un dev normal lo haría mal.
- **Salida**: `app.audio().play_music("level1")` hace **todo** — resuelve formato (P61/PT/MED), reserva en Chip, monta
  el buffer de descompresión, registra la tarea de frame y arma el DMA; `stop_music()` deshace. El juego nunca ve
  `os::*`, `_P61_dma`, `MusicFormat` ni `reserve<AudioTag>`.
- **Nota**: unificar el vocabulario (ver §8): `play_sfx`/`play_music` en `AudioSystem`; `sfx()` deja de exponerse a juego.
- **Progreso**: ✅ el `App` **avanza la música en su latido** (`on_vblank` → `audio().update_music()`); la 213 ya **no** llama `os::set_frame_task` ni tiene `music_tick`/`m_audio`. ✅ `play_music` **resuelve el formato** (detección por cabecera) y el **buffer de descompresión P61** (el engine reserva en Chip si el módulo lo pide, `audio_system.hpp`): el juego no ve `p61_needs_sample_buffer` ni `reserve<AudioTag>`. ✅ `App::play_music(name)`/`stop_music()` atan `assets().music(name)` + `audio().play_music(...)`; la música **por escena** se hace en el `enter` (la siguiente `play_music` detiene la anterior, §6).

## 3. `Screen` de juego y ocultar el display

- **Estado (corrección de la revisión)**: las **primitivas ya existen** — `Screen::clear`, `fill(Box,color)`,
  `frame`, `line`, `text`, `clear_box`, `sprite`, `erase_sprite`, `blit`, `c2p`. No es una carencia de API.
- **Problema real**: la demo 213 **no las usa** (copia `bitplanes().raw()` a mano) y el montaje del display mete
  `SceneResources`/`ocs_a500`/`BPLCON0` (`comp::display(res, 0x5200)`) en código de juego.
- **Salida**: el juego dibuja solo con `Screen`; el display se pide a alto nivel ("fondo 320x256, N planos, esta
  imagen/paleta") y `Scene`/`Band`/`BPLCON0`/copperlist quedan tras el motor y el escape.
- **Progreso**: ✅ `App::start()` compone la escena desde un `GameDisplay` **declarativo**
  (geometría + paleta + `intents` de copper por línea) y hace el `takeover`; la **213 ya no
  nombra** `SceneResources`, `planar`, `compose`, `ocs_a500`, `BPLCON0`, `bind_scene` ni
  `takeover`. Medido: 213 a **49,9 fps** (1 campo) tras la migración. El gradiente de la 213 se
  declara como `GameDisplay::intents` (etapa `composition::intents`), sin que el juego vea la
  copperlist. ⏳ el `GameDisplay` cubre un display planar base; multi-capa (DPF), tilemaps y el
  reparto de recursos siguen en el planner pendiente (§7).

## 4. Assets tipados con formato resuelto

- **Estado (corrección)**: existen `App::assets()` (`res::AssetRuntime`), `AssetTable` (blobs por nombre/dominio) y
  `res::load<Tag>`. No faltan del todo.
- **Problema real**: la 213 **no los usa** (emplea `INCBIN` + `res::load` + un `memcpy` a `bitplanes().raw()`), y no
  hay accesores de **dominio** con el **formato** resuelto (`assets().sprite("hero")` → `Sprite`, `.music("n")`,
  `.sfx("n")`, `.tilemap("n")`).
- **Salida**: `auto hero = app.assets().sprite("hero"); app.audio().play_music(app.assets().music("n"));` — sin
  `INCBIN`, sin `Block<Tag>`, sin memcpy, sin `reserve<AudioTag>`.
- **Progreso**: ✅ `eng::Assets` (`eng/api/assets.hpp`): `add<Tag>` (copia a Chip) + `music/sprite/bytes/palette`
  **por nombre**; la 213 ya **no** usa `res::load` ni `Block<BobTag>/<MusicTag>` (sprite y música por nombre). ✅ el
  audio resuelve **formato** (detección por cabecera) y **buffer** (§2). ✅ bitmap planar por nombre conserva
  geometría/layout y se dibuja con `Screen::bitmap`; HOST-234 verifica datos y segmentación de jobs para
  interleaved 320×256×5, y WinUAE muestra el bitmap en la 213. ✅ el `INCBIN` **sale del código de juego** a un
   **manifiesto** (`demos/.../213/src/assets.manifest.hpp`) que expone los blobs por accesores
   (`abyss::img_data()`/`img_size()`…); el código de juego ya no tiene rutas de assets ni `INCBIN` (medido:
   49,9 fps y misma imagen). ✅ **generador implementado**: `tools/assets/gen-manifest.mjs` emite el header
   desde `assets.manifest.json` —blobs + **geometría** (dimensiones/planos/frames)— más
   `register_assets(Assets&)` que registra todo en una llamada; `Assets::add_sprite`/`sprite(name)` guardan y
   recuperan la **geometría del sprite**, así el juego escribe `m_assets.sprite("bob")` sin `desc`. La 213 usa
   ya el manifiesto generado (misma imagen, READY en WinUAE) y el gate `tools/check/asset-manifests.mjs`
   garantiza que el header no diverge del JSON (corre en `run-host-tests.sh`/`test-regression.sh`). Detalle:
   `docs/tools/ASSET_MANIFEST.md`. ⏳ falta el **pipeline binario cocinado** (UAF-R) para que el `path` sea un
   asset empaquetado en vez de un `.bpl` suelto (hoy `INCBIN`), y geometría de audio si un juego la necesita.
- **Decisión (geometría del sprite)**: el `desc` (ancho/alto/planos/frames/stride) **viaja en el manifiesto**
   (JSON) y el engine lo guarda al registrar (`Assets::add_sprite`); el código de juego no lo escribe. El formato
   binario de los blobs sigue siendo el del pipeline (UAF-R aparte).

## 5. Actores, animación y colisión (2D)

- **Problema**: montar un BOB es campo a campo (`bob.sheet/width/height/planes/frame_count/frame_stride/layout/draw/mask_pack`);
  no hay animación ni colisión de juego (aunque exista `SpriteCollisionConfig`/`BobLayer`).
- **Salida**: `Sprite`+`Anim` (frames, duración, `update()`), un `BobLayer`/`Actor` con orden/prioridad, y consultas de
  colisión simples (caja/píxel). El juego escribe `sprite(anim,i,x,y)`.
- **Progreso**: ✅ **colisión de caja** ya existe (`eng::Box::overlaps`/`intersection`). ✅ **`eng::graphics::Anim`**
  (`anim.hpp`): secuencia de frames con duración por frame, bucle/`play_once`, `update()`/`frame()`/`reset()` (la
  demo escribe `screen().sprite(sheet, anim.frame(), x, y)`); sin reservas ni copias (vistas no propietarias).
  ✅ **animación ligada**: `scene::BobActor` lleva su `graphics::Anim` (vistas no propietarias); `BobLayer::tick()`
  la avanza y `emit` usa su frame (sin que el juego lleve el índice). ✅ **orden/prioridad**: `BobActor::z` y
  `BobLayer::emit` ordena por `z` (inserción estable, sin heap); la fachada lo expone con `Screen::bobs(layer)`
  sin que el juego vea `FramePlan`/`BobTarget`. HOST-354 lo cubre. ✅ **hojas heterogéneas**: `BobActor::sheet_index`
  elige entre las `BobLayer::kMaxSheets` hojas. ✅ **demo con gate visual**: `demos/techniques/amiga/os/215_app_bobs`
  (4 BOBs con `Anim`, orden por `z` —rojo delante del amarillo— y dos hojas; doble buffer sin tearing). ⏳ falta el
  `Anim` **desde el asset** (geometría incrustada por el pipeline, §4) y más hojas por capa si un juego lo pide.

## 6. Escenas/estados de juego

- **Problema**: no hay un gestor de estados (title→game→gameover) en la fachada.
- **Salida**: `app.set_scene(...)`/`push`/`pop` con `enter/exit/update/render`; la música por escena se apoya en §2.
- **Progreso**: ✅ `App::push_scene`/`pop_scene`/`set_scene`/`scene_depth` (`game.hpp`): pila de escenas
  **sin heap ni vtable** (thunks de puntero a función, capacidad fija `kMaxScenes`). La escena superior
  **sustituye** a `update`/`render` del `Game` (el `Game` sigue siendo el *composition root*); `enter`/`exit`
  en las transiciones; hooks opcionales detectados con `requires`. HOST-240 (dispatch, `enter`/`exit`,
  `set_scene`, hooks opcionales y capacidad). ✅ **música por escena**: `App::play_music(name)`/`stop_music()`
  (§2) permiten que el `enter` de cada escena arranque su tema y una escena sin música la silencie; la
  siguiente `play_music` detiene la anterior. ⏳ falta que la 213/plantilla usen una escena de título real.

## 7. Cámara y tilemap de juego

- **Problema**: `XlimitedScene`/`field` son potentísimos pero de bajo nivel (scroll, modulos, EHB).
- **Salida**: un `Tilemap`/`Camera` de juego que por debajo use ese motor; el juego escribe `camera.move(dx,dy)` y
  `map.tile(x,y)`.
- **Progreso (interfaz de scroll)**: el vocabulario de juego ya no baja al metal. La capa declara su
  scroll con `scene::ScrollSpec` (técnica + **período del mapa** en words + velocidad) vía
  `Layer::set_scroll_spec(...)`; `scene::plan_region` deduce coste/memoria y el anillo correcto
  (`scroll_ring_words`), degradando si no cabe (`degrade_scroll`: `Strip → Fine → None`). La técnica
  `ScrollKind::Strip` (camino rápido de tiras, 50 fps) se dimensiona con
  `field::StripScrollGeometry<…, MapWords>`, que deriva `ring = visible + período` y **garantiza por
  `static_assert`** que la `span` del puntero es múltiplo del período del mapa (el fallo de contenido
  al envolver queda imposible por construcción; HOST-244 lo verifica con un invariante de **contenido**
  —ventana visible + palabra extra de fetch—, no solo de "pintado"). `Camera2D` (`move_by`/scroll) y
  `World::add_tile_layer`/`Layer::camera()`/`Layer::tilemap()` ya existen. ✅ el **driver del camino de
  tiras** es reutilizable: `field::StripScrollController<Geom, Map, Sink>` reúne CPU+Blitter
  (`plan_strip_frame` → `compose_column` → blit) y la demo 128 ya **no** reimplementa la lógica; HOST-244
  lo verifica end-to-end (contenido de la ventana + palabra extra). ✅ **capa de fachada**
  `field::StripScrollLayer<Geom, Map, Backend>`: agrupa buffers + controlador + compositor; el juego solo
  declara mapa/banco/paleta/tamaños y conduce con `frame()` (la demo 128 ya no ve el compositor). ✅ **`App`
  la conduce**: `App::add_scroll_layer(layer)` la arranca (memoria + backend) con un asa *type-erased*
  (`ScrollLayerHandle`) y la conduce por frame tras el `update` del juego (`pump_scroll_layers`); HOST-240 lo
  cubre con una capa mock. ✅ **asset de tilemap** `field::TilemapView` (banco + mapa + paleta) ligado con
  `StripScrollLayer::set_tilemap` (el juego no escribe el adaptador). ✅ **demo `App`** =
   `demos/techniques/amiga/playfield/204_app_strip_scroll` (App + capa de tiras; el juego no ve el compositor;
   scroll suave validado con Ollama). ✅ **seam público cerrado**: la fachada `eng/api/scroll.hpp`
   (incluida por `api.hpp`) expone el **vocabulario** (`eng::ScrollSpec`/`ScrollKind`/`Camera2D`) y los
   **motores** sin que el juego incluya `eng/field/*` (la 204/205 ya solo incluyen la fachada); la capa
   acepta la **cámara del juego** por `track_camera` (posición px, para mapas toroidales) o
   `follow_camera(camera)` (cualquier cámara con `x()`/`y()`, p. ej. `scene::Camera2D`, para mapas
   acotados). ⏳ falta el **planner** que elija el motor **solo** (sin que el juego nombre
   `Strip`/`Xlimited`) y una **cámara toroidal** (la `Camera2D` recorta a un mundo acotado); y que el
   **pipeline** (§4) genere el banco ya empaquetado + el mapa como asset tipado (`app.assets().tilemap("n")`).

## 8. Unificar el vocabulario

- **Problema**: `Scene` vs `scene::World` vs `graphics::composition::Scene`; `field` vs `graphics`;
  `DisplayDesc`/`Band`/`RasterLayout`/`SceneResources`; `AudioSystem::play_sfx` vs `SfxMixer::play_on`.
- **Salida**: un nombre por concepto en la capa pública; `field`→`playfield`; `Scene` reservado para el de **juego**.
  Regla ya escrita («una mecánica por eje, menos tipos») aplicada de verdad.
- **Progreso**: ✅ `scene` desambiguado (`D7` de `ENGINE_STRUCTURE_REVIEW.md`). ✅ la fachada de juego
  troceada por tema (`api/screen.hpp`, `api/display.hpp`, `api/world_render.hpp`; `api/game.hpp` de
  familia); ver `D13`. ✅ **`field`→`playfield` completado**: el namespace canónico es ahora
  `eng::playfield` (renombrado en todo el repo —engine, demos, tests—) y **`eng::field` queda como
  alias deprecado** (`namespace field = playfield;` en `field/playfield.hpp`) para código externo no
  migrado. ⏳ pendiente menor: `AudioSystem::play_sfx` vs `SfxMixer::play_on`.

## 9. Plantilla y tutorial

- **Problema**: no hay una vía "plantilla de juego" ni un tutorial de fachada.
- **Salida**: `games/template/` con los 8 primitivos y `docs/…/TU_PRIMER_JUEGO.md`. La **213 migrada a esa plantilla**
  es la validación: si la 213 queda **sin una sola línea técnica**, el API está listo.

## 10. Orden de ataque (por dolor/valor)

1. **§2 Audio auto-conducido** (contenido, mecanismo ya conocido, el fallo más frecuente).
2. **§3 `Screen` de juego** (elimina el 90 % del bajo nivel de las demos/juegos).
3. **§4 Assets con formato resuelto** (mata `res::load`+`INCBIN`+buffer manual).
4. **§1 Arranque cero-config** (`ENG_GAME_MAIN`).
5. **§5 Actores/animación/colisión**.
6. **§6 Escenas/estados**, **§7 Cámara/tilemap**.
7. **§8 Unificación de vocabulario** (transversal; se hace al tocar cada módulo).
8. **§9 Plantilla + tutorial + migración de la 213** (la prueba de fuego, al final).

Cada paso se cierra cuando la 213 (o el "juego de 30 líneas" de §0) puede escribirlo sin la línea técnica que ese paso
elimina, y con la evidencia de siempre (build + run + suite + checks).

## 11. Modelo de memoria unificado (observación de diseño)

Principio: **todo componente que reserva memoria lo hace por las clases del engine** (`MemorySystem`
arenas / `MemoryManager` bancos), nunca `AllocMem` suelto. Estado:

- **Ya se cumple en el engine**: `Scene` (bitplanes/framebuffers), `copper::DoubleBuffer` (copperlist),
  `Bitmap`, `GlyphCache`, `SpriteManager`, `XlimitedScene` (tile banks), `TileScroll`, `SfxMixer` y
  `AudioSystem` (música) reservan de `memory.chip`/`memory.slow`; el único `AllocMem` es el del backend
  en `configure_memory`.
- **Hueco**: **63 demos** reservan su bloque de copper/bitplanes a mano
  (`backend.memory().chip.allocate_block<…>`) y montan la copperlist. Eso debería hacerlo el
  **componente** (una `Scene`/`Screen` dueña de su copperlist), no la demo: al migrar a la fachada ese
  `Block<Tag>` desaparece.
- **`configure_memory`** sigue siendo manual (§1): el juego no debería elegir pool ni tamaños.

## 12. Auditoría de coste del bucle (regla `CODING_STYLE`)

Camino `update`/`render` de la 213 revisado con la regla «coste ~cero en el bucle»:

- **`Screen::sprite`/`clear_box`/`blit`**: encolan un `BlitJob` en el `FramePlan` (array de capacidad fija, sin
  reservas); `Sprite::draw` calcula los jobs **sin divisiones**. OK.
- **`Screen::present`/`Scene::commit`**: ejecuta el plan (Blitter) y parchea los `BPLxPT` (unos `MOVE` de copper).
  Es el trabajo del frame, no sobrecarga. OK.
- **Efectos dinámicos en la ruta planar — RESUELTO**: la copperlist se construye **una sola vez** en el setup
  (`compose`) y por frame solo se parcheaban los `BPLxPT`; los `plan.add(...)` de efectos por frame
  (`Gradient::frame`, `SpriteLayer::frame`) **no se materializaban** (no-op silencioso: la animación se perdía).
  El `Plan` registra ahora, en su `materialize()`, la **palabra de dato** de cada intención materializada
  (`slot_word`/`slot_reg`/`slot_line`); el efecto la **ata** tras el materialize del setup
  (`RasterGradientEffect::bind_slots`) y por frame **parchea solo esas palabras** (`patch_into`), sin re-emitir.
  La API lo expone como `Gradient::bind(scene)` (setup) + `Gradient::patch(scene)` (frame). Verificado con
  **HOST-134** (los colores cambian de fase en su sitio, no solo la estructura). `SpriteLayer::emit_into`
  (emite O(líneas×canales)) queda como candidato al mismo tratamiento cuando se use por frame.
- **`AudioSystem::update_music`** (lo llama `App::on_vblank`): `P61_Music` + armado de DMA, una vez por VBlank. OK.
- **`App::on_vblank`**: `os::tick` (entrada/timers) + tick de audio + post del puerto. Acotado. OK.
- **Zonas de paleta (`scene::zone_color` en 030/040) — OK**: la lista se compone **una vez** en el setup
  (`scene::compose` + `palette_patchable(..., &zone)`) y por frame solo se **parchean** las palabras de color de
  la zona (`zone_color(sched, zone, i).set(...)`). Mismo patrón «construir una vez + parchear» que el degradado;
  no re-emite la copperlist. El `PaletteCycleEffect` (040) también parchea, no reconstruye.
- **`effects::Gradient::frame` — RESUELTO**: `RasterGradient` **precalcula en setup** una tabla `[fase][banda]`
  (`rebuild` en `configure`/`set_keys`/`set_cyclic`) y el bucle **solo copia la fila** de la fase (`phase_index`:
  **máscara** si `k` es potencia de dos, si no un `%`). Cero divisiones por banda (antes ~`5*bands`: el `div_wide` más
  los 3 `divs.w` de `lerp444`). Coste en RAM: `max_keys*max_bands*2` bytes (2 KB con los máximos). Equivalencia de
  colores verificada con **HOST-134**. Bonus: la tabla usa fases `0..k-1` → no sufre el truncado `s16` del original
  cuando `phase` es grande (la 213 usa `phase = frame`).

## 13. Relación con otros documentos

- Visión de las dos capas: [`GAME_API_TWO_LEVELS.md`](../../engine/architecture/GAME_API_TWO_LEVELS.md).
- Audio (contrato actual): [`MUSIC_PLAYER.md`](../../engine/architecture/MUSIC_PLAYER.md), [`GAME_AUDIO.md`](../../engine/architecture/GAME_AUDIO.md),
  [`AUDIO_MIXER.md`](../../engine/architecture/AUDIO_MIXER.md).
- Display/composición: [`DISPLAY_COMPOSITION.md`](../../engine/architecture/DISPLAY_COMPOSITION.md), [`OBJECT_SYSTEM.md`](../../engine/architecture/OBJECT_SYSTEM.md).
- Estado global: [`ROADMAP_UNIFICADO.md`](ROADMAP_UNIFICADO.md).
