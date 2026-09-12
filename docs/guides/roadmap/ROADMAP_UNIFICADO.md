# Roadmap unificado del engine Amiga (reunificación)

Fecha: 2026-09. Este fichero **reúne** los roadmaps/disposiciones dispersos y fija
el estado real del engine y de las demos, para decidir por dónde seguir.

## Inventario de roadmaps / documentos de dirección

| Documento | Estado | Qué cubre |
|---|---|---|
| `docs/guides/roadmap/XLIMITED_8WAY_EHB_201.md` | roadmap original 201→engine→202 | F1-F4 del corkscrew 8-way EHB de 201; **F5 = «Demo 202 DPF (+ features del 107)»** (DPF 3+3 + transparencia, corkscrew, variantes, telemetría) |
| `docs/guides/roadmap/REGLAS_PIPELINE_TILES.md` | reglas vigentes | Pipeline tiles/EHB (reglas de oro, no romper el 100 %) |
| `docs/demos/tile-pipeline/PIPELINE_TILES_EHB.md` | guía completa | Cuantizar → slice → emit → verificar (round-trip 100 %) |
| `docs/guides/roadmap/PROBLEMA_LAUNCHER_DEMOS_NUEVAS.md` | RESUELTO | Demos nuevas que quedaban en AmigaDOS |
| `docs/guides/roadmap/TILED.md` | conocimiento preservado | Formato Tiled (.tmx/.tsx), a usar cuando haya metadatos |
| `docs/engine/architecture/DPF_MIXTO_SPLIT_LINEAL.md` | (nuevo) | DPF: modos de Y por campo (split/lineal/mixto) |

## Estado real del engine y las demos (2026-09)

- **Scroll**: corkscrew 8-way X-Limited (`XLimitedPlayfield` + `ScrollEngine` +
  `ScrollSink`), tiles interleaved de 320 px, wrap toroidal, anillo vertical.
- **DPF 3+3** (`XlimitedDualComposer` + `XlimitedScene`): dos playfields con
  banco/mapa/paso propios, transparencia del FG (PF1 color 0), `BPLCON1` por
  campo; bancos reales por campo a 3..6 planos (`blocks_prebuilt`, `blocks_prebuilt2`).
- **Y por campo (flexible, paramétrico, sin macros)**: `dual_linear_field` +
  `linear_display` + split por campo en el compositor. Modos: corkscrew dual
  (Y compartida), lineal dual (Y independiente en ambos, 2× mirror) y mixto.
- **Demos**: 107 (corkscrew/DPF showcase), 201 (EHB mapa real, tour 8-way), 202
  (DPF 3+3 mapa real 8c + plaquettes, recorrido con Lissajous, por defecto FG
  lineal/Y propia + BG corkscrew/split, variante `kShareY`).
- **Telemetría por frame** leíble (símbolo en `.data`), lectura arreglada.
- Sin `linear_display` por defecto en 202: viewport recortado 320×208 para split
  canónico; mapas toroidales.
- **Matemática y assets (import demoscene, 2026-09)**: `eng/core/math2d.hpp`
  (lib2d: matrices 2×2 4.12 + `clip_line`/`clip_polygon`) y `eng/core/math3d.hpp`
  (lib3d: `Mat3x3`, rotaciones, `compose`, `face_visible`); modelo de malla en
  `eng/core/mesh3d.hpp` (`MeshView` + `mesh_transform` + `mesh_painter_order` con
  culling y shell sort) — tests HOST-010/011/013.
  Base del contenedor de assets UAF-R: `eng/assets/uaf.hpp` (`Blob` valida
  header/chunks por offset; consumidores `PaletteView`/`BitplanesView`/`SampleView`/
  `StringsView`/`TilesView`/`SpritesView`/`CopperView`/`MeshAssetView`, test HOST-012)
  con exportador host `tools/assets/uaf-pack.ts` (chunky→planar, sprites/copper/malla
  `obj2c`, `packUaf`/`parseUaf`, doc `docs/tools/UAF_PACK.md`). La cola de
  blits/presupuesto ya existía (`frame_plan.hpp`); el borrado del sólido la usa vía
  `MinimalBackend::execute_frame_plan` (Blitter). Validación en hardware con las demos
  `077_math3d_cube` (alambre) y `078_math3d_solid` (relleno por tramos de byte/word,
  malla cargada desde un blob UAF-R incbinado y borrado por Blitter). `mesh_painter_order`
  soporta caras de doble cara; gate visual `tools/analyze/verify-math3d-cube.mjs`.
  Relleno por Blitter en `MinimalBackend::fill_triangles_blitter` (line + area fill
  en máscara + cookie-cut, portado de `libblit`/amiga-bootcamp) y `blit_fill_from_mask`
  (máscara CPU + cookie-cut Blitter) tras `-DK_FILL_BLITTER=1`. WIP: ambas rutas Blitter
  dibujan pero el resultado sale rayado (alineación/carry de los canales A/C-D), así que
  el relleno por CPU (tramos de byte/word) es el defecto y las de Blitter quedan en
  depuración. Para depurar se usan capturas + `tools/analyze/ollama-desc.mjs` (visión).

## Sprites hardware — estado (2026-09)

- **Hecho**: `SpriteTemplate` + `SpriteManager::emit_template_into` (multiplexado
  vertical "chasing the raster" + color multiplexing) validados por la demo 053.
  Se corrigió la codificación de `SPRxPOS`/`SPRxCTL` (VSTART byte alto, HSTART÷2 en
  byte bajo; ver `amiga-bootcamp/08_graphics/sprites.md`) y los offsets de registro
  (antes caían en registros de audio).
- **Pendiente (mejoras apuntadas)**:
  - `SpriteAllocator` (paso 4 de `ENGINE_DESIGN.md` §5): asignar canales a
    `SpriteIntent` con multiplexado y decidir el overflow → BOB (transición
    sprite→BOB transparente). Es el siguiente paso. **Hecho**: la lógica pura
    (`sprite_allocator.hpp`) está validada por el test host HOST-003.
    `SpriteManager::emit_into` (camino de 8 canales, demo 054): **arreglado el
    "no dibuja"** añadiendo `wait_line_safe(vstart)` antes de cada sprite (el
    sprite debe programarse en su VSTART, no arriba del frame); también corregido
    `copper_words()` (6→10 words/sprite). **Pendiente (bug secundario)**: los
    sprites dibujan pero TODOS en azul (COLOR25) y agrupados, en vez de
    rojo/verde/azul/amarillo en fila; queda por diagnosticar (paleta/posición con
    8 sprites en el mismo VSTART).
  - Diagnosticar por qué **rellenar bitplanes rompe el rearm** del sprite en modo
    6 planos (solo dibuja el primer segmento); bloquea fondos reales en demos de
    sprites. Alternativa segura: fondo por copper-gradient (`COLOR00` por línea).
  - Materializar los `CopperIntent` que faltan (`ShiftLines`, `BitplaneSplit`,
    `SpriteRearm`, `Priority`) en el driver que conoce el layout (paso 2/3).
    **Hecho**: `CopperScheduler::emit_copper_intents_full`/`emit_single_intent`
    materializan ya `PaletteLine/PaletteSpan/BitplaneSplit/ShiftLines/
    SpriteRearm/Priority`; HOST-002 ampliado y en verde. `StaticEhbScene::
    rebuild_copper` emite las zonas de paleta como `PaletteLine` (demos 030/040
    llegan a READY).
  - Embellecer la demo 053 (fondo, animación de colores) siguiendo la regla
    «Demos atractivas» de `AGENTS.md`.

## Input y audio — estado (2026-09)

- **Hecho (input, paso 6 de `ENGINE_DESIGN.md` §5)**: `eng::input::InputAggregator`
  (`PadState`, `MouseState`, `KeyState`) en `engine/include/eng/input/input.hpp`,
  validado por HOST-004. Backend de lectura corregido en `eng/platform/input_poll.hpp`:
  direcciones por `JOY0DAT`/`JOY1DAT` (código de Gray por eje; arriba/abajo con XOR),
  fuego por `CIAAPRA` bits 6/7, y `poll_input(InputAggregator&)`. Decodificación pura
  validada por HOST-006 (mismo mapeo que los motores ACE/Sevgi y el AHRM). Demo
  `056_input_aggregator` (cruz móvil + fuego + tecla) compila/ejecuta a READY.
- **Hecho (input, ratón + CD32)**: `poll_mouse` (delta por `JOY0DAT` + botones por
  `CIAAPRA`/`POTINP`), `read_cd32_buttons` (protocolo serie `POTGO`) y
  `decode_cd32_buttons` (puro) en `input_poll.hpp`. `PadState` ampliado con
  `reverse`/`forward`. Decodificación CD32 validada por HOST-007 (mismo mapeo que
  Sevgi). **Pendiente**: validar la lectura serie en emulador (sensible a timing).
- **Hecho (audio SFX, paso 7 de `ENGINE_DESIGN.md` §5)**: `eng::audio::SampleEvent`,
  `MusicEvent`, `AudioPlan` y `AudioMixer` en `engine/include/eng/audio/audio.hpp`,
  validado por HOST-005. **Integrado de forma nativa el Audio Mixer 3.7 (Photon)**:
  código ASM en `support/audio_mixer/` (ensamblado con VASM a ELF en `build-demo.sh`),
  envoltura de juego `eng::audio::SfxMixer` en `engine/include/eng/audio/sfx_mixer.hpp`.
  Config actual: `MIXER_SINGLE`, salida `DMAF_AUD0`, 4 voces software, 11 kHz,
  `MIXER_C_DEFS=1`. Demo `058_sfx_mixer` reproduce un bucle + beep; `DMACONR`=0x381
  (`AUD0EN`+`DMAEN`) y `MixerGetTotalChannelCount()`=4. Documentado en
  `docs/engine/architecture/AUDIO_MIXER.md` (requisitos de muestras preprocesadas,
  capacidades, configuración, API, preprocesado, rendimiento). Herramienta host
  `tools/audio/sample-converter.ts` (+ test) para preprocesar muestras (±32, múlt. 4).
- **Hecho (música, `MusicPlayer`)**: reproductor **P61** (Photon/Scoopex) importado
  de `demoscene-repo-orig/lib/libp61` a `support/music/` (`p61.asm` +
  `P6112-Play.i`, ensamblado con VASM). Envoltura de juego `eng::audio::P61Player`
  en `engine/include/eng/audio/music_player.hpp` (API sin punteros: `MusicModule`
  = `Span<const u8>`). Demo `059_music_player` enlaza SFX mixer + P61 y llega a
  READY (`P61_Init` falla limpiamente sin módulo). **Importado** también
  **ptplayer** (Frank Wille): `support/music/pt.asm` + `ptplayer.i` + `support/vbr.s`
  (`_ExcVecBase=0`). Envoltura `eng::audio::PtPlayer` (modo CIA) y demo
  `060_music_pt`: genera un `.mod` mínimo en memoria y reproduce música real
  (`DMACONR` con `AUD0EN` activo tras ~100 frames). **Consolidado** en
  `eng::audio::AudioSystem` (`engine/include/eng/audio/audio_system.hpp`): fachada
  única que compone `SfxMixer` + `P61Player`/`PtPlayer`; demo `061_audio_system`
  demuestra coexistencia SFX (AUD0) + música (AUD1) con el orden canónico
  música→canal reservado→mixer. Añadido `set_master_volume` (volumen global SFX+
  música). **Capa de juego** `eng::audio::GameAudio` (`game_audio.hpp`) +
  `SampleBank`/`allow_trigger`/`allow_group` (puros, `sfx_bank.hpp`): banco de
  sonidos por `id`, política de voces (cooldown, límite de instancias, agrupación,
  prioridad) y ducking; validado por HOST-008 y demo `062_game_audio`. **El audio
  lo posee ahora el backend** (`MinimalBackend::audio()`/`audio_init()`); el
  `GameAudio` se enlaza con `attach(backend.audio())`. Guía completa (API +
  generación de música y sonidos desde herramientas externas) en
  `docs/engine/architecture/GAME_AUDIO.md`.
  **Pendiente**: `libahx` (necesita `.BIN` + libc).
  Ver `docs/engine/architecture/MUSIC_PLAYER.md`.
- **Regla de API aplicada**: la capa de audio no expone punteros crudos al
  programador; las áreas de memoria contigua se representan con `eng::Span`
  (`SfxSample` y `MusicModule`). Los punteros quedan en la capa interna
  (`MixerEffect`/wrappers `jsr`).
- **Pendiente (audio, parado 2026-09 por tiempo)**: el mixer de 4 voces con
  **samples one-shot** solo deja oír ~2 voces al mezclar una caja de ritmos
  (`074_mixer_drums`). El **loop** sí se oye en las 4 (`076_mixer_sample_channels`,
  estilo 075 de tonos). Los 4 canales reciben disparos (contadores 4:2:8:1), así que
  se sospecha del camino **one-shot** del mixer (o del balance perceptual); siguiente
  prueba: disparar los 4 samples one-shot a la vez, y si falla, pasar 074 a
  loop+`stop()`. Otros arreglos ya hechos y documentados en `AUDIO_MIXER.md`:
  buffers de plugin no nulos (o el mixer queda en silencio), desbordamiento de
  `(f<<22)` para f>1024, generación de tono sin discontinuidad de bucle
  (`synth_tone`/`synth_sequence`) y tool `tools/audio/prep-sample.ts`
  (WAV→raw→remuestreo→normalizar/escalar, con test).
  **Rendimiento**: con el mixer activo el bucle cae a ~2-5 fps emulados (vs ~normal
  sin mixer); `MIXER_WORDSIZED`/`MIXER_SIZEX32` NO mejoran → el coste es
  por-interrupción (IRQ de audio + reprogramación de DMA), probablemente exagerado
  por WinUAE; **validar en hardware real**. Opción a probar: `MIXER_EXTERNAL_IRQ_DMA`
  (la doc permite que el IRQ/DMA los lleve el motor/OS). Ver `AUDIO_MIXER.md`
  §"Rendimiento medido en el emulador".

## Tests host — estado (2026-09)

- **g++ nativo instalado** (WinLibs GCC 16.2.0, mingw-w64 ucrt) en
  `C:\Users\dvdjg\Documents\programa\AI\Amiga\mingw64`. No está en el PATH: para
  correr `tools/run-host-tests.sh` usar
  `CXX="C:\...\Amiga\mingw64\bin\g++.exe" bash tools/run-host-tests.sh`.
- **Corregido** el cast puntero→`u32` (portabilidad 32/64 bits): las cabeceras de
  gráficos/field ahora usan `eng::uintptr` para extraer direcciones de punteros
  (`copper.hpp`, `sprite_manager.hpp`, `tile_scroll.hpp`, `dpf_composer.hpp`,
  `xlimited.hpp`). En m68k `uintptr == u32` (no-op).
- Tests host: HOST-000 (math), HOST-001 (graphics_driver_contract), HOST-002
  (raster_intent), HOST-003 (sprite_allocator), HOST-004 (input), HOST-005
  (audio), HOST-006 (input_decode) y HOST-007 (input_cd32) **pasan**. El **ICE de
  gcc 16.2.0** de HOST-001 quedó resuelto moviendo `SineTable` a definición
  out-of-class en `xlimited_scene.hpp` (bug del compilador, no del código).

## Decisiones tomadas en 202 (a respetar)

1. El scroll debe ser **un único algoritmo toroidal** (sin modos de borde finito);
   el recorrido se acota a un primer paso del mundo para no mostrar la costura.
2. Sin `linear_display` global salvo elección explícita; para Y independiente se
   usa el modo por campo (split + lineal/mirror).
3. Sin macros de selección en C++: constantes paramétricas (`if constexpr`) o
   plantillas; el engine configura por datos.
4. Telemetría y `analyze-sequence.sh` como gate por demo (movimiento continuo +
   regresión de la Y independiente).
5. Pipeline de tiles por defecto: paleta `mediancut` + dither `atkinson` a 8
   colores. Se añadió el dither `checker` (checkerboard de dos colores cercanos,
   estilo core-design, para sintetizar colores perdidos tras la cuantización) y
   los nombres de tiling incluyen tamaño de tile y dither (`…c_tN_<paleta>_<dither>…`).

## F6 — Variante rápida «Sonic» (medición E1, 2026-09)

Objetivo: pasos >2 px/frame (5-6 px, estilo Sonic) garantizando que la fila/
columna entrante se dibuja siempre a tiempo, y tiles de 32×32.

Medición E1 (parámetro `kFastStep` en la 202, sin macros): con `kFastStep=6` la
telemetría real da `blit_jobs` máx 27 (media 12) y `blit_words` máx 1296 → carga
baja para 50 fps; el cruce de 16 px pinta su columna/fila dentro del mismo frame
(paint-then-advance) → **no hace falta draw-ahead por coste** a estas velocidades.

Pendientes de F6:
- Verificar/arreglar el camino completo de tiles **32×32** (banco X-Limited,
  fetch/guardas, fillup) en el engine y validarlo en una demo/variante.
  Estado: el direccionado del banco en `draw_block_job` ya es genérico (320 px →
  `320/tile` bloques/fila, words = tile/16). **Bloqueo de algoritmo**: el 8-way
  X-Limited recorre el tile píxel a píxel con `fetch_scroll_pixels = 16` y
  `BPLCON1` de 4 bits (0..15), es decir está pensado para tiles de 16; con tiles
  de 32 el fine del chipset (máx 16) no cubre el tile completo. Opciones:
    A) Tratar el tile de 32 como dos macro-pasos de 16 (redibujo/plane-shift cada
       16 px dentro del tile) — extensión del algoritmo.
       Diseño (resumen A): el 8-way conserva el fine de 16 (BPLCON1 0..15) y el
       pointer avanza cada 16 world-px; cada tile de 32 se divide en DOS «macro-
       celdas» de 16 que se redibujan por separado según cruce. Concretamente:
       - `draw_block_job` ya escribe words=tile/16 (2 en 32) → dibuja el tile
         completo cuando se pinta su celda.
       - El scroll (scroll_right/left/down/up) pasa a cruzar cada 16 px de la
         macroc el del (en lugar de cada tile): al cruzar el 1er macro-paso de un
         tile 32 se muestra la mitad ya dibujada; al cruzar el 2º se redibuja el
         siguiente tile. Es un cambio acotado a `scroll_engine.hpp` (granularidad
         de cruce = 16 fijo, no tile) + el fetch/guarda de `tile_width`.
    B) En el test DPF BG=32/FG=16, usar scroll 16 para el BG (tiles de 32 solo
       como «macro-celda» de contenido, movimiento 8-way cada 16) — sin cambiar
       el fine del algoritmo.
       Nota tras análisis: con fine 16 y celdas de 32 «de contenido», el test se
       reduce a la demo actual de 16; no ejercita de verdad tiles de 32. Solo la
       opción A valida 32 real en 8-way.
    C) Posponer 32 hasta documentar bien A.
- Decidir si a saltos ≥ 2 cruces/frame conviene pre-dibujar N columnas/filas según
  la velocidad (opción paramétrica del scroll, sin tocar la regla de dibujo).

## Preguntas abiertas y próximas direcciones (a decidir)

Opciones (no excluyentes, conviene priorizar):

- **A. Capa de bobs/personajes en el FG lineal** (estilo Megatyphoon: escribir
  solo donde el bob no solapa a otros, sin releer pantalla). Aporta el “juego de
  plataformas” real sobre la base DPF.
- **B. Varias capas de parallax** (BG mero fondo + FG plataformas 8-way, estilo
  Jim Powers): demostrar/validar con 2+ capas y Y por capa.
- **C. Mapa/editor y metadatos**: retomar Tiled (metadatos, objetos, spawns) y el
  camino completo de pipeline hacia un nivel jugable.
- **D. Escenas/juego end-to-end**: elegir un caso de uso (plataformas o shooter 8
  way) y construir la primera escena «de juego» encima del engine parametrizado,
  cerrando el F5 de forma real (harness por feature).
- **E. Robustez/rendimiento**: telemetría con gate estricto 50 fps en las demos y
  presupuesto de Chip RAM por modo DPF (matriz de memoria documentada).

Recomendación inicial: **D** (caso de uso concreto, p. ej. plataformas con BG
fondo + FG mapa 8-way, reutilizando 202/DPF) para cerrar el F5 y forzar el resto
de decisiones (bobs, Tiled, memoria) con un objetivo real; en paralelo **E**
ligero (gate 50 fps + tabla de memoria) para no perder el presupuesto.

## Plan — Shooter/side-scroller con ejes limitados + RoboCod (2026-09)

Decisión (usuario): la familia de scroll es **XYLimited** con **modo por eje**
(`Ring` / `Finite` / `Off`, `ScrollMode::OneDirection`). Un juego usa un eje
**largo** (anillo) y el otro **corto** (finito, sin guardas). DPF siempre, con el
FG dedicado a **objetos** (naves, disparos) y el BG al tilemap de scroll. Se
desarrolla en varios turnos; el orden es 1→2→3.

### Parte 1 — Shooter vertical puro (primero)
- Mundo **400 px de ancho × 2048 px de alto**; tiles 16×16 → **25×128** celdas.
- **Tileset de 128 tiles**. Mapa = índices `u16` (25·128 = 3200 words ≈ 6.3 KB); el
  framebuffer de scroll sigue **acotado** (solo índices/tileset crecen).
- **X `Finite`** (recorrido 0..80, sin anillo ni guardas laterales), **Y `Ring`
  one-direction** (solo la fila entrante; la nave no vuelve a bajar),
  `display_height = 256+32`, cámara inicial a media altura (`set_camera`).
- **DPF**: BG = tilemap XYLimited; FG = capa de objetos (planos pares) para
  naves/disparos.
- Demo prevista: `demos/amiga/110_ylimited_shooter`.

### Parte 2 — Side-scroller horizontal (después)
- Mundo **4096 px de ancho × 320 px de alto**; tiles 16×16 → **256×20** celdas.
- **Tileset de 128 tiles**. **X `Ring`** (XLimited, scroll largo), **Y `Finite`/
  `Off`** (alto corto). **DPF** con FG de objetos.
- Requiere simetría de ejes: `y_mode` (`Finite`/`Off`) y X `Ring` largo (hoy X
  `Ring` ya existe; falta el `y_mode`).
- Demo prevista: `demos/amiga/111_xlimited_sidescroller`.

### Parte 3 — XYLimited 5 planos con fondo estilo RoboCod
- Escena **XYLimited de 5 bitplanes**; el **fondo** usa el truco **RoboCod**
  (un plano con offset de scroll propio), **mapeando las paletas para que queden
  16 colores** visibles.
- Trabajo de engine: **parallax por plano** (offset por plano sumado en
  `draw_block_job` **y** en la emisión de `BPLxPT`, con la cámara como fuente y
  envolviendo en el anillo) + **patrón de tiles propio** del plano de fondo
  (admite tiles grandes, p. ej. 64×64; el Blitter y el tileset ya soportan
  múltiplos de 16).

### Estado del engine para este plan (2026-09)
- ✅ `AxisMode` (`Ring`/`Finite`/`Off`) en `XlimitedConfig` y `XLimitedPlayfield`.
- ✅ X `Finite`: bitmap = ancho de mundo, puntero directo, sin creep ni guardas
  (`ScrollEngine::scroll_right/left`); `set_camera(x,y)`; `one_direction` corregido
  en `scroll_left`/`scroll_up`.
- ✅ `x_mode` reenviado por `XlimitedSceneConfig`; `bg().set_camera(...)` disponible.
- ✅ Test host **`023_limited_axes`** (scenario del shooter: X finito + Y anillo
  one-direction).
- ✅ Parte 1: demo **`110_ylimited_shooter`** (400×2048, tileset 128, X `Finite`,
  Y anillo one-direction) **con DPF**: BG = corkscrew XYLimited (PF1) + FG = lienzo
  de objetos (PF2, delante) dibujando nave y disparos; cámara inicial abajo,
  telemetría de cámara en `detail`, `analyze` OK. (Falta pulido visual de los
  objetos.)
- ✅ Engine: `XlimitedDualConfig.foreground_is_pf2` (PF2 delante), validación del
  compositor DPF relajada (corkscrew + lienzo estático) y **módulo de PF2 corregido**
  (usaba fetch 40 estándar en vez del 42 del corkscrew → el lienzo se descuadraba en
  líneas).
- ✅ Parte 2: demo **`111_xlimited_sidescroller`** (4096×320, 256×20 tiles, tileset
  128, **X `Ring`** con mapa toroidal, **Y fijo** `scroll_y=false`, DPF con FG de
  objetos: nave con barrido vertical + balas), `analyze` OK y telemetría de cámara.
  Pendiente fino: `y_mode` `Finite` para un Y corto *con* scroll (hoy Y fijo), si un
  juego lo necesita.
- ⏳ Parte 3: parallax por plano (RoboCod) + patrón/tileset del plano de fondo.
- ⏳ Transversal: soporte de **tileset de 128 tiles** (ya usado en 110/111) y de tiles
  grandes (64×64) en el pipeline/`BlocksBitmap`; matriz de memoria Chip por modo DPF.
