# Composición de escenas: el modelo de tres planos

Este documento fija cómo el engine describe y gestiona **escenas** (configuraciones del
hardware Amiga) de forma versátil, sustituyendo la familia de "drivers" por efecto. Nace del
problema observado al importar efectos de `demoscene-repo-orig`: cada efecto traía su propia
configuración cableada como una clase (`StaticEhbScene`, `CopperChunkyScene`, `CanvasScene`,
`TileScrollScene`), con tres consecuencias.

## 1. El problema

- **Reutilización forzada**: una configuración creada para un efecto se acaba usando para
  otra cosa, ignorando lo que no aplica (un driver EHB usado como display planar plano con
  `row_repeat = 1`, sin `bplcon1_shift`).
- **Interfaces arbitrarias**: cada driver expone métodos elegidos a dedo; no hay un contrato
  común más allá del `concept` `GraphicsDriver` (`bind` + `bitplane_bytes_for` + `takeover`/
  `install`).
- **Explosión combinatoria**: importar 50 efectos podría significar decenas de clases casi
  iguales, inmanejable.

Y, sin embargo, cada aplicación necesita **configurar el hardware con precisión quirúrgica**
y **cambiarlo en runtime** (un juego que usa un efecto por pantalla).

## 2. El modelo: tres planos separados

Una escena se describe en **tres planos** ortogonales, cada uno con su dueño:

```text
   RECURSOS                 PROGRAMA                     COMPORTAMIENTO
   (qué hace falta)         (timeline de registros)      (CPU + eventos)
   ┌────────────────┐       ┌────────────────────┐       ┌──────────────────┐
   │ bitplanes      │       │ lista de Copper     │       │ setup / teardown │
   │ layout/geom.   │       │ (por scanline)      │       │ tarea por frame  │
   │ paleta/sprites │       │ + puntos de parcheo │       │ IRQ (VBlank/blit)│
   │ buffers (N)    │       │ + presupuesto       │       │ patch handles    │
   └────────────────┘       └────────────────────┘       └──────────────────┘
        POD, fijo                data (Copper)               código + datos
```

- **Recursos**: memoria Chip y registros que la escena ocupa (geometría, planos, paleta,
  sprites, nº de buffers). Estructura POD de capacidad fija (sin heap).
- **Programa**: el **timeline de registros por scanline**. Es *data* ejecutada por el
  **Copper** (ver §3). Lo emite el `copper::Scheduler`/`copper::Plan`; la escena solo lo
  construye (una vez o por frame) y lo **parchea**.
- **Comportamiento**: lo que **no** es Copper (C2P, rellenos, transforms, blits) y los
  **eventos** del ciclo de vida. Tareas sin virtuales (`function_ref` o functors), con
  buffers del llamador.

## 3. El Copper es el intérprete: el coste de "data-driven" es casi nulo

La objeción al enfoque data-driven es el coste de interpretar en runtime. **En Amiga no
aplica si el artefacto es una lista de Copper**: el chip la ejecuta por DMA (coste CPU ≈ 0) y
la CPU solo **parchea** valores. Por eso este diseño separa lo *estructural* (palabras de
Copper, que se construyen una vez) de lo *variable* (handles de parcheo, que se escriben por
frame).

La regla: **si la descripción es estructural (cerca del registro) es data; si es semántica
("scroll de playfield") hay que traducirla cada frame y se paga**. El engine elige
estructural, con bloques de alto nivel que **expanden en tiempo de construcción**, no en el
hot path.

## 4. Composición: etapas, presets, handles y tareas

Las piezas se unen por **composición de etapas** (builder plano; el *expression template*
es una optimización posterior, §5), y cada escena concreta pasa a ser un **preset** (una
función), no una clase.

```text
   compose(recursos,
       display(diw, ddf, planos, layout),      // etapa: emite BPLCON0/DIW/DDF/BPLxPT
       palette(base) | zones(sky),             // etapa: paleta base + zonas raster
       row_repeat(4) | bplcon1(0x22) | reverse_ptrs,  // features verticales/orden
       surface(viewport),                       // etapa opcional: expone Playfield/Surface
       on_setup(carga), on_vblank(fill), on_blitter_done(chain), on_teardown(libera),
       patch<u16> scroll_x, patch<Palette> sky) // handles de runtime
```

- Cada **etapa** contiene: (a) lo que **pide** (recursos), (b) lo que **emite** (Copper),
  (c) opcionalmente lo que **ejecuta** (tareas) y lo que **expone** (handles).
- Los **presets** son funciones que devuelven una escena compuesta:
  `ehb_preset(base, zones)`, `ham_preset(...)`, `planar4_preset(...)`. No hay clases por
  efecto.
- Los **handles de parcheo** son el punto de "precisión quirúrgica": la app escribe un
  valor por frame y el engine resuelve qué palabras de Copper tocar (sin interpretar nada).
- Las **tareas** cubren el ciclo de vida (`setup`, por frame antes/después de VBlank,
  IRQ de blitter, `teardown`), con la firma del engine (`function_ref` sin heap).

### Cambio de escena en runtime

Cambiar de efecto = **cambiar el programa/recursos**. Con `MultiBuffered` + `copper::Plan`
ya hay doble buffer de display y de copperlist: una escena viva puede **reconfigurar** su
sub-programa (p. ej. una `ModeSwitchZone`, o reemplazar un track de paleta) sin dejar de
mostrar. Añadir una escena nueva en runtime = construir su programa en el bloque trasero y
publicarlo en el swap, no instanciar una clase.

## 5. Gramática: builder ahora, expression templates después

El primer paso es un **builder plano** (funciones y composición de structs) que emite data y
declara recursos. Es legible, depurable y sin bloat. Sobre él, si hace falta, se puede
construir un **expression template** que exprese la misma composición como *tipo*, con el
compilador plegando etapas y features a código óptimo; **siempre que el artefacto siga
siendo data (Copper)**, no código interpretado. El ET debe reservarse a la **estructura**
(qué etapas, en qué orden, con qué features), y exponer los **valores variables** como
handles, no como nodos del árbol.

Guardarraíl: medir *code bloat* y tiempo de compilación si se usa ET a fondo (ya pesó en
`eng/core/expr.hpp`).

## 6. Guardarraíles

- **Coste hardware visible**: el programa y su `copper::ScheduleReport` (waits/moves por
  línea, overflow) quedan auditables; los presets no lo esconden.
- **Sin heap ni virtuales** en el hot path; capacidades fijas (pools/arenas). El cambio de
  escena en runtime no asigna.
- **Presupuesto** de Copper por scanline (`Plan`) y de Blitter por frame (`FramePlan`/
  `BlitBudget`).
- **Escape hatch crudo** siempre disponible (`raw_copper(...)`: MOVEs sueltos) para efectos
  que no encajen en las etapas.
- **Un solo núcleo, sin familia**: la fontanería (bloques, `bind`, accessors, `install`/
  `takeover`) vive en un único tipo (`Scene`/`HardwareProgram`); los presets la usan por
  composición. **No** hace falta una base CRTP: no hay varias clases hermanas de las que
  extraerla.

### 6.1 Validación de configuraciones contra el backend (`scene/limits.hpp`)

`SceneResources` puede expresar combinaciones que la función acepta pero el **hardware no
permite** (p. ej. `width = 300` en Amiga, o 7 planos por playfield en un A500). Eso es una
dependencia del **backend**, no del modelo: por eso el perfil de capacidades es un dato
declarado por la máquina y la validación es agnóstica.

```text
   SceneResources ──┐
                    ├──► validate(res, limits) ──► ConfigError{ code, message }
   DisplayLimits ───┘         (constexpr)              ok() / !ok()
      (perfil)          │
                        ├─ consteval valid_scene()  → static_assert  (config en compilación)
                        └─ runtime                  → init/compose(..., limits) + config_error()
```

- **`DisplayLimits`**: qué admite el backend. Cubre **fetch horizontal** (`DDFSTRT` mín
  `0x18`, `DDFSTOP` máx `0xD8`, ≤ 25 palabras lores = 400 px fetchables, 368 px visibles por
  blanking — AHRM Tabla 3-14), altura (PAL 256), planos **por modo**
  (normal/DPF/HAM/EHB), modos soportados, buffers y **coste de bus** (`slots_per_line` = 226,
  `fixed_dma_slots` = 27, `fetch_width_max`). Perfiles como **datos**: `ocs_a500`
  (6 planos, DPF 3+3, HAM6/EHB6, fetch 1×), `ecs` (idéntico en lores), `aga_a1200`
  (8 planos, DPF 4+4, HAM8, fetch 4×). Otro backend (Mega Drive, Neo Geo) declara el suyo.
- **`SceneMode`** (`Standard`/`Ham`/`Ehb`/`DualPlayfield`) en `SceneResources`: determina qué
  límite de planos aplica (p. ej. DPF en OCS = 3+3; HAM6/EHB = 6; HAM8 en AGA = 8) y qué
  `BPLCON0` genera `bplcon0_for(mode, planes)`.
- **Geometría derivada**: `geometry_for(res)` produce DIW/DDF coherentes con `width` (DDF
  estándar `0x38` + palabras de fetch) si `res` no los especifica; la etapa
  `display(res, bplcon0=0)` usa `geometry_for` + `bplcon0_for(mode, planes)`.
- **Coste de bus** (`DmaCost` / `dma_cost(res, limits, fw)` + `FetchWidth`): informativo, **no**
  es validez. `bitplane_slots = palabras_de_fetch × planos / fw` y `cpu_slots = slots_per_line −
  (bitplane_slots + fixed_dma_slots)`. A 320 px y 6 planos OCS: 120 + 27 = 147 → 79 slots de
  CPU (~35 %). En AGA, `fmode` 4× reduce el coste ×4 (8 planos = 40 slots). Fuente:
  `amiga-bootcamp/01_hardware/common/dma_architecture.md`.
- **Estática (compilación)**: `consteval bool valid_scene(res, limits)` → `static_assert`
  cuando la config se conoce al compilar:
  `static_assert(scene::valid_scene(scene::planar(288, 256, 4), scene::ocs_a500));`
  (336 es válido; 384 supera los 368 visibles; 300 no es múltiplo de 16; 7 planos exigen AGA).
- **Dinámica (ejecución)**: `compose(scene, mem, res, limits, etapas...)` valida antes de
  reservar; el rechazo queda en `scene.config_error()` (`code` + `message`), con código por
  causa (1 ancho, 4 alto, 5 planos, 8/9 modo, 10 DDF…). El perfil es **obligatorio**: no hay
  variante de `compose` sin `limits`. Las demos usan `display(res)`, que deriva geometría
  (`geometry_for`) y `BPLCON0` (`bplcon0_for(mode, planes)`).
- **Huella estática de etapas**: las etapas de **forma conocida** exponen su tamaño en
  palabras como `constexpr` (`display_words`, `palette_words`, `palette_zone_words`,
  `reverse_ptrs_words`, `patchable_zone_words`, `intent_words`/`intents_words`,
  `row_repeat_words`), comparable con `copper_word_budget(res)` en un `static_assert`;
  HOST-016/215 verifican que las fórmulas coinciden con la emisión real
  (`scheduler().words_used()`/`Scene::words()`) y la demo 081 usa el gate en compilación. El
  modelo es **lores**; hires/SuperHires/`DIWHIGH` (ECS/AGA) se retomarán cuando haya un
  consumidor (ver `pending-verification.md` §6).
- **Rendimiento**: las validaciones de copperlist (presupuesto por línea, overflow) viven en
  `materialize`/`end_frame` (**una vez por frame**), nunca en la emisión por MOVE
  (`move`/`wait` son `always_inline` y no validan). Regla: **estáticas siempre; dinámicas solo
  en setup/cierre de frame**.

### 6.2 Rasterizado CPU/Blitter (`field/raster.hpp`)

`Surface` es el **contexto de dispositivo**: expone la misma API
(`set_pixel`/`draw_line`/`fill_rect`/`fill_polygon`/`blit`/`blit_masked`/`draw_text`) sin que
el consumidor sepa si detrás hay CPU o Blitter. El seam está en `field::Rasterizer`:

- `RasterOp` (`Copy`/`Or`/`And`/`Xor`/`Clear`): operación lógica de una escritura, uniforme
  para CPU (lógica de palabras) y Blitter (`BlitJob::minterm`).
- `RasterCaps` (lo declara el **backend**: hay Blitter, ancho de bus, fill/line/shift/minterms)
  y `RasterPolicy` (lo elige la app: `AccelMode::Auto`/`Cpu`/`Blitter`, umbral de área,
  `cpu_fast`).
- `CpuRaster` (relleno por `draw_span_op`; copia por `Playfield::copy_rect_cpu`, con stores de
  32 bits en 68020+) y `BlitterRaster` (relleno por `fill_polygon` → `PolygonFillSink`/Blitter;
  copia por el `FramePlan`).
- `Scene::set_raster(rasterizer, policy)` elige la implementación; `Surface` la lee del
  playfield. El `PolygonFillSink` existente queda como una de las operaciones del seam.

Extensión pendiente: relleno de rect **directo por Blitter** (BLTCON fill) y copia
enmascarada por CPU con rutas por target.

## 7. Relación con lo que ya existe

| Plano | Pieza existente | Falta |
|---|---|---|
| Recursos | `Block<Tag>`, `MemorySystem`, `MultiBuffered` | un `SceneResources` POD que agrupe geometría/layout/buffers |
| Programa | `copper::Scheduler`, `copper::Plan`, `CopperIntent`, `ModeSwitchZone` | un `HardwareProgram` que agrupe la lista + recursos + presupuesto |
| Comportamiento | `FramePlan` (blits), `function_ref`, IRQ/VBlank | tareas de ciclo de vida homogéneas y handles de parcheo tipados |
| Composición | — | `compose(...)` + etapas + presets |

## 8. Migración

1. `StaticEhbScene` → etapas `display`+`palette`/`palette_zones` (hecho en 030).
2. Display planar paramétrico → `scene::planar` + etapas `row_repeat`/`reverse_ptrs` (hecho;
   `planar_scene.hpp` retirado).
3. `CanvasScene` → `layout = Interleaved` + `surface()` (hecho, HOST-212); el layout contiguo
   también expone `surface()` vía `ContiguousPlayfield`.
4. `CopperChunkyScene` → etapa `copper_chunky(...)` (lista propia, sin bitplanes).
5. Demos/tests migran a las etapas; se conservan las clases como `using`/shim hasta cerrar.

## 9. Verificación

- Test host por etapa (emisión de Copper esperada) y del compuesto (orden/presupuesto).
- Test host de los handles de parcheo (escribir un handle cambia la palabra correcta).
- Gate de codegen (`codegen-report.mjs`) del hot path de tareas/handles: sin libcalls ni 68020.
- Demos como consumidor real (`build -> run -> analyze`).

Referencias: `DISPLAY_COMPOSITION.md` (buffers y copper), `GRAPHICS_DRIVERS.md` (estado
actual y criterio), `3D_RENDER_VS_PHYSICS.md` (frontera render), `CODING_STYLE.md` (§`inline`).

## 10. Callables del ciclo de vida (plano de comportamiento)

La tarea del comportamiento es **`eng::util::FunctionRef<void()>`** (alias `scene::Task`), la
referencia **no propietaria** del engine (análogo de `std::function_ref`), no `std::function`:
el runtime es freestanding (sin `libstdc++`, sin heap, sin excepciones) y `std::function`
reservaría y arrastraría `<functional>`. `FunctionRef` son **dos punteros** y acepta lambdas
(functors) directamente.

- **Vida**: no posee el callable → debe vivir más que la escena (buffer del llamador). No
  construir la tarea desde un temporal; usar un lambda **con nombre**.
- **Secuencias que esperan ticks** (setup/teardown largos): reutilizar
  `eng::util::TaskSequence<N>` + `TaskStatus` (`eng/core/util/task.hpp`), no inventar otro
  patrón.

### Coste medido (68000, `-O2`, `out/tmp/task-probe.cpp`)

| Caso | Instrucciones | Llamadas |
|---|---:|---:|
| Tarea vía `FunctionRef` en punto **opaco** (parámetro) | 10 | 1 (`jsr (aN)` indirecta) |
| Mismo cuerpo por **plantilla** (functor inline) | 5 | 0 |

Una tarea por invocación cuesta **~1 llamada indirecta (~20–25 ciclos)** y pierde el inlining
del cuerpo. Para el ciclo de vida **por frame** (unas pocas tareas) es **despreciable** frente
a los ~142 000 ciclos de un campo, así que `FunctionRef` se mantiene: da un `Scene` **no
plantilla** y uniforme. Si un hook fuera **por scanline o por elemento**, ahí sí conviene un
hook por **plantilla** (inline, sin `jsr`), a cambio de templatizar el programa (bloat).

Nota: a `-O2`, un lambda **local conocido** se inlinea incluso a través de `FunctionRef`; el
`jsr` aparece solo cuando el callable llega **opaco** (guardado y llamado desde otro punto),
que es el caso real de una tarea almacenada.

### ¿Hace falta poseer el callable? (`InlineFunction<Sig,N>`)

Evaluado: **no ahora**. El llamador ya es dueño del lambda; `FunctionRef` basta. Si algún día
se necesitara guardar un cierre **por valor** con tipo uniforme sin variable con nombre
(p. ej. tareas creadas al vuelo), la pieza sería una `eng::util::InlineFunction<Sig,N>` con
**SBO de capacidad fija** (sin heap) y un `static_assert` del tamaño del cierre; no existe
todavía y se añadiría **solo con consumidor real**. Límite a documentar entonces: `N` debe
cubrir el cierre (unas decenas de bytes; 3–4 punteros suele bastar).

## 11. Estado del prototipo

`engine/include/eng/graphics/composition/compose.hpp` (verificado por HOST-215):

- **Recursos**: `SceneResources` (geometría, `rows` lógicas, planos, `layout` contiguo/
  interleaved, tamaño de copper).
- **Escena**: `Scene` posee bitplanes + copperlist + `Scheduler` + tareas + `surface()` (con
  layout interleaved, sobre un `CanvasPlayfield` interno).
- **Etapas**: `display` (contigua o interleaved), `palette`, `palette_zones`,
  `patchable_zone` (genérica) + `palette_zones_patchable` (caso particular), `row_repeat`,
  `reverse_ptrs`, `intents` (CopperIntent ordenadas por el `Plan`).
- **Base común de parcheo**: `PatchSlot` (registro + valor), `PatchZone` (grupo en una
  línea, `handle(s, i)`), `PatchHandle` (un MOVE) y `Patch32` (valor de 32 bits = pareja de
  MOVEs, p. ej. `BPLxPTH`+`BPLxPTL`, caso **multi-registro**). Sirve para **cualquier** valor
  dinámico del copper (colores, `BPL1MOD/BPL2MOD`, `BPLxPT`, `BPLCON1`…), no solo paletas.
- **Constantes y helpers**: `DisplayGeometry`/`kPal320x256` (en `limits.hpp`),
  `kBplcon0_{4Planes,4PlanesNoColor,Ehb,Ham6}`, `bplcon0_for(mode, planes)`,
  `display_words`/`palette_words`/`palette_zone_words`/`reverse_ptrs_words`/
  `patchable_zone_words`/`intent_words`/`intents_words`/`row_repeat_words`/
  `copper_word_budget` (huella estática de etapas).
- **Configuración**: una sola función paramétrica `planar(width, height, planes)`; los
  escenarios (EHB = 6 planos, HAM/cuadruplicado = `rows` + `row_repeat`, canvas =
  `layout = Interleaved`, doble buffer = `buffers = N`) van en el doc-comment.
- **Composición**: `compose(scene, memory, recursos, limits, etapas...)`; `Scene::init_raw`
  es privado (la única vía pública valida el perfil).

**Validado en demo**: `081_background_tasks` migrada a `scene::compose` (display+palette) con
el latido de COLOR00 como `Scene::on_frame` → **49.75 fps (142 576 ciclos = 1 campo)**; el
ciclo de vida cuesta ~474 ciclos/frame. Migradas también al modelo (display EHB + paleta)
las 3D `077_math3d_cube`, `078_math3d_solid` y `084_mf_rotation`, sin `install` por frame
(la lista es estática: `takeover` la instala una vez); verificadas `build -> run -> analyze`.

**Pendiente**:
1. **Doble buffer de display**: **hecho** en layout contiguo. `SceneResources.buffers` (1/2/3)
   reserva N bitmaps; la etapa `display` emite los `BPLxPT` como MOVEs **parcheables**
   (`move_at`) y `Scene::commit()` repunta los punteros al buffer trasero (reutiliza la base
   común de parcheo). El interleaved usa un único `CanvasPlayfield`.
2. **Migrar 030 (EHB)**: **hecho** (usa `palette_patchable`/`palette_zones`).
3. **Migrar 080 (HAM+C2P)**: **hecho** (`display`+`row_repeat`+`reverse_ptrs`+`buffers=2`).
4. **Retirar drivers obsoletos**: `planar_scene.hpp`/`ehb_scene.hpp` (`StaticEhbScene`) y
   `canvas_scene.hpp` (`CanvasScene`) **retirados** (HOST-001 usa un `MockGraphicsDriver` local y
   HOST-212 usa `CanvasPlayfield`); `EhbPalette`/`black_palette` sustituidos por
   `eng::Palette32`/`eng::kBlackPalette` (`palette32.hpp`). `CopperChunkyScene` **se mantiene**: no
   es un «scene» planar sino una **técnica de display por Copper** (bloques de `COLOR00`, sin
   bitplanes) que `scene::compose` no expresa (exige ≥1 plano); 082/083 lo usan con
   `MultiBuffered`. Retirarlo exigiría un modo copper-chunky en el modelo de composición.
5. **Medición**: el `runner.uae` lo genera `run-demo.ts`; en entornos sin Git Bash se
   construye a mano para `measure-fps` (como se hizo con 081).




