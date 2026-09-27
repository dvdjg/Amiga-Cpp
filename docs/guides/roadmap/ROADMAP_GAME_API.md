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

## 1. Arranque cero-config

- **Problema**: el juego llama `app.configure_memory({96k, 8k, 4k, 0})` y `comp::compose(...)` con `ocs_a500`,
  `SceneResources` y un `BPLCON0` crudo (`comp::display(res, 0x5200)` en 213).
- **Salida**: `ENG_GAME_MAIN(Game)` bootea hardware (detección en `eng::hw`), elige memoria y un display por defecto
  (320x256, planos según el fondo que pida el juego), y llama `init/update/render`. El juego **no** nombra
  `MemoryConfig`, `SceneResources`, `ocs_a500` ni registros.

## 2. Audio auto-conducido (el eslabón con más dolor)

- **Problema**: la música **no se conduce sola**. La 213 hace `app.memory_manager().chip().reserve<AudioTag>(need, 4)`,
  `p61_needs_sample_buffer`, `os::set_frame_task(&music_tick, this)` y (por el bug del playroutine) necesita que el
  engine re-arme el DMA. Un dev normal lo haría mal.
- **Salida**: `app.audio().play_music("level1")` hace **todo** — resuelve formato (P61/PT/MED), reserva en Chip, monta
  el buffer de descompresión, registra la tarea de frame y arma el DMA; `stop_music()` deshace. El juego nunca ve
  `os::*`, `_P61_dma`, `MusicFormat` ni `reserve<AudioTag>`.
- **Nota**: unificar el vocabulario (ver §8): `play_sfx`/`play_music` en `AudioSystem`; `sfx()` deja de exponerse a juego.
- **Progreso**: ✅ el `App` **avanza la música en su latido** (`on_vblank` → `audio().update_music()`); la 213 ya **no** llama `os::set_frame_task` ni tiene `music_tick`/`m_audio`. ⏳ falta: que `play_music` **resuelva el formato y el buffer de descompresión** (que el juego no vea `p61_needs_sample_buffer`/`reserve<AudioTag>`).

## 3. `Screen` de juego (5 primitivas) y ocultar el display

- **Problema**: hoy hay **siete** tipos para "dibujar" (`Screen`, `DrawTarget`, `Surface`, `Playfield`,
  `CanvasPlayfield`, `Rasterizer`, `FramePlan`) y el juego **sí** toca `Scene`/`Band`/`BPLCON0`/`bitplanes().raw()`
  (213 copia la imagen a mano).
- **Salida**: una sola superficie de juego con `cls(color)`, `rect(x,y,w,h,color)`, `line`, `text(x,y,cstr)`,
  `sprite(anim,i,x,y)`, `blit`. `Scene`/`Band`/`BPLCON0`/copperlist quedan **solo** para el motor y el escape.

## 4. Assets tipados con formato resuelto

- **Problema**: la 213 hace `INCBIN` + un `for` de memcpy a `bitplanes().raw()`; y `res::load<Tag>` para bob/mod/pal;
  el juego gestiona Chip y alineación.
- **Salida**: `app.assets().sprite("hero")`, `.music("level1")`, `.sfx("boom")`, `.tilemap("cave")`, `.palette("x")`.
  El engine resuelve **formato**, medio (Chip cuando toca), alineación y `INCBIN`/pipeline. Devuelve handles de dominio
  (`Sprite`, `Anim`, `Sound`), no `Block<Tag>` ni punteros.

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

## 11. Relación con otros documentos

- Visión de las dos capas: [`GAME_API_TWO_LEVELS.md`](../../engine/architecture/GAME_API_TWO_LEVELS.md).
- Audio (contrato actual): [`MUSIC_PLAYER.md`](../../engine/architecture/MUSIC_PLAYER.md), [`GAME_AUDIO.md`](../../engine/architecture/GAME_AUDIO.md),
  [`AUDIO_MIXER.md`](../../engine/architecture/AUDIO_MIXER.md).
- Display/composición: [`DISPLAY_COMPOSITION.md`](../../engine/architecture/DISPLAY_COMPOSITION.md), [`OBJECT_SYSTEM.md`](../../engine/architecture/OBJECT_SYSTEM.md).
- Estado global: [`ROADMAP_UNIFICADO.md`](ROADMAP_UNIFICADO.md).
