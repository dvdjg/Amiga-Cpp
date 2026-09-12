# Diseño integral del engine Amiga (C++23 freestanding)

Este documento es el diseño de referencia del engine: las capas, las entidades y sus
dependencias para **un juego completo** (gráficos, E/S, sonido, música, escena, assets y
ciclo de vida), el encaje de las fuentes externas (demoscene-repo, amiga-bootcamp, ACE,
Sevgi_Engine) y la **conversión del sistema actual** descrita paso a paso.

Sustituye y absorbe a `VISUAL_EFFECT_SPRITE_DESIGN.md` (que queda como referencia de la
parte gráfica/efectos). La libreta de la que parte es el `ROADMAP_ENGINE_CPP_AMIGA500.md` §20;
aquí se concreta con nombres de tipos, namespaces y dependencias.

## 0. Principios (invariantes de diseño)

1. **El juego no ve registros ni DMA.** Todo hardware queda detrás de una capa backend y
   de schedulers que son dueños únicos del Copper/Blitter/Sprites/Paula.
2. **Retained-mode.** El juego actualiza una descripción (`RenderScene`/`GameWorld`) y el
   engine la compila a un `FramePlan` por frame. Nada dibuja directamente.
3. **Sin heap en gameplay, sin exceptions, sin RTTI** (reglas de `CODING_STYLE.md`).
4. **Presupuestado y medible.** Cada coprocesador expone su coste estimado; el engine
   degrada o advierte antes de corromper la imagen (regla de `DmaBudget`/`Timeline`).
5. **Una abstracción es válida si elimina decisiones repetidas, mantiene visible el coste y
   se verifica con test/demo.** De lo contrario no sube a `engine/`.

## 1. Mapa de capas (de arriba a abajo)

```
┌───────────────────────────────────────────────────────────────────┐
│ Game (lógica de juego, demoscene, tools de demo)                  │  sin Amiga
│  EntityId + Componentes + intenciones de alto nivel               │
└───────────────────────────────┬───────────────────────────────────┘
                                │
┌───────────────────────────────▼───────────────────────────────────┐
│ GameWorld / SceneGraph2D / Camera2D / Physics2D / Interacción     │  sin HW
│ (estado lógico retenido, agnóstico de backend)                    │
└───────────────────────────────┬───────────────────────────────────┘
                                │  compila a intenciones
┌───────────────────────────────▼───────────────────────────────────┐
│ RenderCompiler / AudioCompiler / InputAggregator                  │  sin HW
│  CopperIntent · BlitIntent · SpriteIntent · PaletteIntent         │
│  SampleEvent  · MusicEvent                                         │
└───────────────┬───────────────────────────────────────────────────┘
                │  arbitraje (dueños únicos, presupuestado)
┌───────────────┼──────────────┬──────────────┬─────────────────────┐
│ CopperSchedul │ BlitterQueue │ SpriteAlloc  │ AudioMixer          │  modelo HW
│ er + Timeline │ + Budget     │ ator + mux   │ (music + sfx)       │
└───────────────┴──────────────┴──────────────┴─────────────────────┘
                │  → FramePlan (gráficos) + AudioPlan (sonido)
┌───────────────────────────────▼───────────────────────────────────┐
│ PlatformBackend (display, input, reloj, audio, debug, memoria)    │  con HW
│  MinimalBackend hoy; ACE como backend interno opcional            │
└───────────────────────────────┬───────────────────────────────────┘
                                │  → AssetRuntime (UAF-R chunks cocinados)
┌───────────────────────────────▼───────────────────────────────────┐
│ Chip RAM / Slow RAM / Fast RAM / registros / Copper / Blitter/    │
│ sprites / Paula                                                  │
└───────────────────────────────────────────────────────────────────┘
```

La novedad frente a lo ya implementado es: **AudioPlan + AudioMixer** (no existe aún el
reproductor), **InputAggregator unificado** (hoy hay `input_poll.hpp` parcial) y
**AssetRuntime** (hoy los assets se incrustan con `incbin`/include sueltos).

## 2. Inventario de subsistemas (entidades del engine)

Cada subsistema es un namespace bajo `eng::` y se lista con sus tipos y dependencias.

### 2.1 Core (`eng::core`) — sin dependencias de hardware
Ya existe y se irá ampliando desde demoscene `libmisc`/`libc`.
- `types` (u8..u32), `span`, `ct_array`, `fast_div`, `sinetable`, `isqrt`, `sort`,
  `crc32`, `random`, `utf8`.
- Nuevo a aportar (demoscene `lib2d`/`lib3d`): `math2d`/`math3d` (`Vec3s`, `Mat3x4`,
  `Transform3D`, `ClipPolygon`, `FaceVisibility`, `SortFaces`) — matemática pura,
  testeable en host (`tests/host/`).

### 2.2 Memoria (`eng::memory`)
- `MemorySystem` (Chip/Slow/Fast), `LinearArena`, `MemoryBlock`, `MemoryReport`.
- Política de backend (`MemoryPolicy`) ya descrita: OS-friendly / mixed / takeover.
- Dependencia: `memory.hpp` no depende de nada más que `core`.

### 2.3 Display y gráficos (`eng::graphics`)
- `driver.hpp`: `GraphicsDriver`/`DisplayDriver` (contrato ya formalizado).
- `copper/`: `ListBuilder`, `Scheduler`, `Timeline` (ya). Crece para aceptar
  `CopperIntent[]` ordenado por franja (el punto de unión con los efectos).
- `frame_plan.hpp`: `FramePlan` (blits, paleta, dirty rects, presupuesto). Crece para
  admitir `SpriteIntent` (asignación de canales) y más presupuestos (DMA unificado).
- `blit/` (nuevo): `BlitterQueue`, `BlitterBudget`, minterms reutilizables. Mapea demoscene `libblit`.
- `drivers/`: `StaticEhbScene`, `HamScene`, `TileScrollScene`, `Standard4/5`, `FakeDPF`,
  `DualPlayfield`, `SpriteBackdrop`, `CopperHeavy` (roadmap §20.5).
- `effects/`: `Effect` concept + `PaletteCycleEffect`, `RasterGradientEffect`,
  `RasterDistortionEffect`, `CopperScript`, etc. (roadmap §20.6).

### 2.4 Objetos/escena (`eng::field`, `eng::scene`)
- `Visual` (contenido portable: BOB/sprite/tile).
- `CopperIntent` (efecto raster por franja: paleta, shift, split, sprite rearm).
- `SpriteTemplate<MaxSegments,MaxSwitches>` (plantillas de sprites con reuso vertical y
  paleta por franja).
- `scene/`: `VirtualScene`, `Camera2D`, `TileLayer`, `RouteCamera` (ya parcial).

Detalle gráfico completo en `VISUAL_EFFECT_SPRITE_DESIGN.md`.

### 2.5 E/S y control (`eng::input`) — ampliar respecto a `input_poll.hpp`
Un solo agregador portable que el backend alimenta:

```
┌──────────────────────────────────────────────────────────────┐
│ InputAggregator (estado lógico por frame, sin HW)             │
│  MouseState   { dx, dy, buttons }                            │
│  PadState     { up, down, left, right, fire, fire2, play,... }│
│  KeyState     { scanned[matrix], hotkeys }                   │
└──────────────────────────────▲───────────────────────────────┘
                               │ alimenta el backend
PlatformBackend (Amiga: potgo/ciaa/ciab + joyport + teclado)
```

- Los controladores de Sevgi (joystick, CD32 joypad en ambos puertos, ratón, teclado) son
  el prior art: el `PadState` debe modelar `fire2/play/yellow/green/red/blue` (CD32) mapeando
  ambas unidades a la misma abstracción.
- Dependencia: `input` depende solo de `core`; el backend traduce registros (`ciaa.pra`,
  `pota`/`potgo`) a `InputAggregator`.

### 2.6 Audio y música (`eng::audio`) — NO existe aún, prioridad
- `SampleEvent` (efecto de sonido) y `MusicEvent` (tracker) como intenciones.
- `AudioMixer`: dueño de Paula (4 canales), mezcla de sfx + música; se alimenta cada frame
  en VBlank. Expone `AudioPlan` (canales, frecuencias, volúmenes, punteros de muestra).
- `MusicPlayer`: wrapper sobre reproductores asm conservados en `support/` (demoscene
  `libp61`/`libpt`/`libahx`/`libctr`; ACE/Sevgi usan `ptplayer`, licencia compatible).
- Dependencia: `audio` depende de `core` + `memory` (muestras en Chip RAM). El mixer es un
  dueño único como el `CopperScheduler`.

### 2.7 Assets y carga (`eng::assets`) — la capa UAF-R
- `AssetRuntime`: `handle` → `Visual`/`SpriteTemplate`/`TileMap`/`Palette`/`Sample`/`Music`
  por **offsets validados** (sin parsing pesado en Amiga). Formato: chunks UAF-R
  (header, palettes, bitplanes, copper templates, patch tables, sprites, BOBs, tiles,
  collision, strings, samples, modules).
- El exportador host genera chunks cocinados a `out/assets/<pipeline>/`; el runtime los
  mapea. Inspirado en la generación de cocinados de Sevgi Editor (bobsheet/spritebank/
  tilemap/tileset/palette) y en el formato de autoría de UAF.
- Estado (2026-09): contenedor + vistas tipadas en `eng/assets/uaf.hpp` (`Blob`,
  `Reader`, `PaletteView`, `BitplanesView`, `SampleView`, `StringsView`, `TilesView`,
  `SpritesView`, `CopperView`, `MeshAssetView` (malla `obj2c`), `BlobWriter`;
  test HOST-012) y exportador host `tools/assets/uaf-pack.ts` (chunky→planar,
  sprites/copper/malla, `packUaf`/`parseUaf`; doc `docs/tools/UAF_PACK.md`).

### 2.8 Ciclo de vida y framework (`eng::engine`)
- `Engine<Backend, Game>` ya existe: `init -> (update -> wait_vblank -> render) x N`.
- Añadir fases explícitas (roadmap Fase 2): `input`, `update fijo`, `prepare render`,
  `blit jobs`, `copper commit`, `sprite commit`, `audio mix`, `swap`. El punto de commit
  visible sigue siendo `render` (sincronizado a VBlank).
- **Trabajo de fondo y tarea ociosa** (`engine.hpp`, `eng/task/background.hpp`): el
  engine posee una `task::BackgroundQueue` (expuesta en `GameContext::background`) y la
  drena durante el hueco de VBlank (`BackgroundPump`, como máximo
  `max_slices_per_frame` rebanadas/frame), con prioridad estricta al bucle principal. Si
  el juego expone `idle(backend, context)`, también se le llama ahí (p. ej. las fases del
  C2P de `fire-rgb`). `MinimalBackend::wait_vblank(task, user)` entrega la línea de raster
  (`vpos`) como hook. Ver `BACKGROUND_TASKS.md`.

### 2.9 Debug y telemetría (`eng::debug`)
`RunStatus`, `DebugPeripheral` (consola/checkpoints/counters) ya existen. Se usan para la
regla de evidencia y el profiling (roadmap §22).

## 3. Dependencias (grafo dirigido, sin ciclos)

```
core ────────────── memory ─────────── graphics ───────── driver/field/scene
        │                                 │
        │            input                ├── effects (depende de copper+frame_plan)
        │                                 │
        └──────────────── audio ──────────┘
                         assets ────────── (depende de graphics+audio+scene)

engine (orquesta) ← backend (implementa lo que el engine pide)
```

Regla: **nada por encima de `backend` depende de registros/DMA**. `core`/`input`/
`math2d`/`math3d` son host-testables (sin WinUAE).

## 4. Ideas originales a extraer de cada fuente (decidido)

| Fuente | Idea a adoptar | Dónde encaja |
|---|---|---|
| demoscene-repo-orig | Librerías `lib2d/lib3d/libblit/libgfx` + asm (c2p/p61) | `core::math2d/math3d`, `graphics::blit`, `support/` |
| amiga-bootcamp | Multiplexado, color multiplexing, "chasing the raster", sprite-as-playfield; presupuesto DMA/copper; antipatterns como invariantes | `SpriteTemplate`, `CopperIntent`, `DmaBudget` |
| ACE | HAL fino (audio/blitter/copper/teclado/ratón), view/viewport estilo OS, debug vs release, OS se deshabilita/re-habilita, scroll tilemap eficiente | `PlatformBackend`, `MemoryPolicy`, `TileScrollDriver` |
| Sevgi_Engine | Editor que genera cocinados (bobsheet/spritebank/tilemap/palette), plantillas por género, controladores CD32, ptplayer, double buffering | `eng::assets` (UAF-R), `input` (CD32), `audio` |
| Universal-Asset-Format | Formato de autoría que el exportador cocina a UAF-R | capa `eng::assets` + tools export |

Lo que NO se adopta de ACE/Sevgi (según lo pedido, solo ideas originales): su sistema de
widgets MUI (Sevgi Editor es una app de editor, no del engine) y su acoplamiento a toolchains
concretos; el engine mantiene su propio `Span`/estilo freestanding.

## 5. Conversión del sistema actual (plan por pasos)

El sistema actual ya tiene: `FramePlan`, `CopperScheduler`, `SpriteManager`, drivers
(`StaticEhbScene`, `TileScrollScene`, `XlimitedScene`), `input_poll`, `engine` loop,
`DisplayDriver`/`GraphicsDriver`, `AssetRuntime` no formalizado. Falta: vocabularlo de
intenciones (CopperIntent unificado), audio, assets, y las piezas 3D/2D.

Orden de conversión (cada paso valida con `build -> run -> analyze` y, si es puro, con test host):

1. **Congelar el vocabulario de intenciones** (`CopperIntent` + concept `Effect`): tipos
   puros, sin HW, con `static_assert`. Hacer que `PaletteCycleEffect` lo cumpla. Es el
   punto de unión de todo lo demás.
2. **Ampliar `CopperScheduler`** para aceptar `CopperIntent[]` ordenado por franja,
   reutilizando `Timeline` como presupuesto. Migrar `emit_palette_zone` a este vocabulario.
3. **Retained scene + actor con `CopperIntent`**: una demo con un BOB que cambia paleta/
   shift por línea (primer "objeto versátil" real), validado por pixel-assert.
4. **`SpriteTemplate` + `VirtualSprite`** (decisión hardware/BOB): multiplexado y color
   multiplexing sobre `SpriteManager`; sprite→BOB transparente.
5. **`Playfields emitiendo `CopperIntent`** (simetría): migrar el `rebuild_copper` de los
   drivers de scroll a intenciones en vez de MOVEs directos.
6. **Input unificado** (`InputAggregator` + backend con CD32): sustituir el uso suelto de
   `input_poll`.
7. **Audio** (`AudioMixer` + `MusicPlayer` sobre asm en `support/`): última pieza de
   hardware; test con música + sfx a 50 fps.
8. **Assets UAF-R** (`AssetRuntime` + exportador host por chunks): cerrar el ciclo
   editor→cocinado→runtime. Es el habilitador de importar escenas completas de demoscene.
9. **Matemática 2D/3D** (`math2d`/`math3d` + malla `mesh3d`) desde demoscene `lib2d/lib3d`,
   con tests host HOST-010/011/013.

Cada paso respeta la regla de oro: si un efecto/actor/playfield toca un registro fuera del
scheduler, falta abstracción.

## 6. Criterio de aceptación del diseño

- Un juego de prueba (micro-aventura EHB de una pantalla, roadmap §11) se escribe **sin
  ninguna referencia a Amiga** en la lógica: pide escena, actores, música, sfx, input y
  efectos por intención.
- Cambiar de driver (`StaticEhbScene` → `Standard4` → `DualPlayfield`) no reescribe la
  lógica de juego.
- Cada subsistema tiene dueño único de su coprocesador y expone presupuesto.
- Los subsistemas puros (`core`, `input` lógico, `math2d/3d`) se validan en host; los de
  hardware, con demo + secuencia + pixel-assert.
