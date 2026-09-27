# Roadmap de la fachada de juego (API de juego limpio)

Estado: **propuesta abierta**. Origen: revisión honesta de lo que la demo 213 necesita realmente escribir (ver
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
- **Salida**: `ENG_GAME_MAIN(Game)` bootea hardware (detección en `eng::hw`), elige memoria y un display por defecto
  (320x256, planos según el fondo que pida el juego), y llama `init/update/render`. El juego **no** nombra
  `MemoryConfig`, `SceneResources`, `ocs_a500` ni registros.
- **Decisión (`configure_memory`)**: el default del engine será «todo lo posible menos headroom» (sin **restringir**,
  regla §1.1 del API), detectado con `hw`/`AvailMem`; el juego podrá **ajustar** (dejará de ser obligatorio
  configurar). Pendiente de implementar.

## 2. Audio auto-conducido (el eslabón con más dolor)

- **Problema**: la música **no se conduce sola**. La 213 hace `app.memory_manager().chip().reserve<AudioTag>(need, 4)`,
  `p61_needs_sample_buffer`, `os::set_frame_task(&music_tick, this)` y (por el bug del playroutine) necesita que el
  engine re-arme el DMA. Un dev normal lo haría mal.
- **Salida**: `app.audio().play_music("level1")` hace **todo** — resuelve formato (P61/PT/MED), reserva en Chip, monta
  el buffer de descompresión, registra la tarea de frame y arma el DMA; `stop_music()` deshace. El juego nunca ve
  `os::*`, `_P61_dma`, `MusicFormat` ni `reserve<AudioTag>`.
- **Nota**: unificar el vocabulario (ver §8): `play_sfx`/`play_music` en `AudioSystem`; `sfx()` deja de exponerse a juego.
- **Progreso**: ✅ el `App` **avanza la música en su latido** (`on_vblank` → `audio().update_music()`); la 213 ya **no** llama `os::set_frame_task` ni tiene `music_tick`/`m_audio`. ⏳ falta: que `play_music` **resuelva el formato y el buffer de descompresión** (que el juego no vea `p61_needs_sample_buffer`/`reserve<AudioTag>`).

## 3. `Screen` de juego y ocultar el display

- **Estado (corrección de la revisión)**: las **primitivas ya existen** — `Screen::clear`, `fill(Box,color)`,
  `frame`, `line`, `text`, `clear_box`, `sprite`, `erase_sprite`, `blit`, `c2p`. No es una carencia de API.
- **Problema real**: la demo 213 **no las usa** (copia `bitplanes().raw()` a mano) y el montaje del display mete
  `SceneResources`/`ocs_a500`/`BPLCON0` (`comp::display(res, 0x5200)`) en código de juego.
- **Salida**: el juego dibuja solo con `Screen`; el display se pide a alto nivel ("fondo 320x256, N planos, esta
  imagen/paleta") y `Scene`/`Band`/`BPLCON0`/copperlist quedan tras el motor y el escape.
- **Progreso**: ✅ `comp::compose(scene, memory, res, paleta)` compone **sin** `DisplayLimits`/`BPLCON0`; la 213 ya
  no nombra `ocs_a500` ni `0x5200`. ⏳ falta: la **imagen de fondo** por la fachada (hoy `bitplanes().raw()` + memcpy).

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
  audio resuelve **formato** (detección por cabecera) y **buffer** (§2). ⏳ falta: el **bitmap de fondo** por nombre
  (`bytes()` existe, pero la 213 copia a mano) y quitar el `INCBIN` del código de juego.
- **Decisión (geometría del sprite)**: el `desc` (ancho/alto/planos/frames/stride) se queda como **dato del juego**
  hasta que exista un **pipeline/tabla** que lo incruste con el blob (cabecera por asset). No se inventa un formato
  ahora: incrustar geometría es decisión del pipeline, no del API.

## 5. Actores, animación y colisión (2D)

- **Problema**: montar un BOB es campo a campo (`bob.sheet/width/height/planes/frame_count/frame_stride/layout/draw/mask_pack`);
  no hay animación ni colisión de juego (aunque exista `SpriteCollisionConfig`/`BobLayer`).
- **Salida**: `Sprite`+`Anim` (frames, duración, `update()`), un `BobLayer`/`Actor` con orden/prioridad, y consultas de
  colisión simples (caja/píxel). El juego escribe `sprite(anim,i,x,y)`.

## 6. Escenas/estados de juego

- **Problema**: no hay un gestor de estados (title→game→gameover) en la fachada.
- **Salida**: `app.set_scene(...)`/`push`/`pop` con `enter/exit/update/render`; la música por escena se apoya en §2.

## 7. Cámara y tilemap de juego

- **Problema**: `XlimitedScene`/`field` son potentísimos pero de bajo nivel (scroll, modulos, EHB).
- **Salida**: un `Tilemap`/`Camera` de juego que por debajo use ese motor; el juego escribe `camera.move(dx,dy)` y
  `map.tile(x,y)`.

## 8. Unificar el vocabulario

- **Problema**: `Scene` vs `scene::World` vs `graphics::composition::Scene`; `field` vs `graphics`;
  `DisplayDesc`/`Band`/`RasterLayout`/`SceneResources`; `AudioSystem::play_sfx` vs `SfxMixer::play_on`.
- **Salida**: un nombre por concepto en la capa pública; `field`→`playfield`; `Scene` reservado para el de **juego**.
  Regla ya escrita («una mecánica por eje, menos tipos») aplicada de verdad.

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
- **`AudioSystem::update_music`** (lo llama `App::on_vblank`): `P61_Music` + armado de DMA, una vez por VBlank. OK.
- **`App::on_vblank`**: `os::tick` (entrada/timers) + tick de audio + post del puerto. Acotado. OK.
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
