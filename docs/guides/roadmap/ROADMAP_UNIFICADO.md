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
| `docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md` | **(nuevo) modelo objetivo** | Separación algoritmo/superficie/composición/máquina de playfields y scroll (contrato de refactor) |
| `docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md` | **(nuevo) roadmap vigente** | Fases para llevar el código actual al modelo objetivo; demos a adaptar (107/110/111/112/201/202) |
| `docs/guides/roadmap/CONSULTA-SPLIT-208.md` | **RESUELTO** | Límite de 8 bits del split vertical del corkscrew (OCS/ECS/AGA), ratificado por IA externa; alternativas (`linear_display` para 256 px) |
| `docs/guides/roadmap/SCROLL_DEMOS_CLEANUP.md` | **(nuevo) Fase 0** | Inventario de demos de scroll, glosario XLimited/XYLimited, lista de salvamento y matriz de demos por algoritmo; tileset 32c compartido |
| `docs/guides/roadmap/NORMALIZACION_REPO.md` | **roadmap vigente de limpieza** | Fases F0-F5 para normalizar tras la mezcla de ramas: una sola implementación de buffers y de copper, superficies sin memoria, unificación de scroll/cámaras, checklist de incongruencias |
| `docs/engine/architecture/DISPLAY_COMPOSITION.md` | **(nuevo) contrato** | Buffers de display (3 granularidades, `MultiBuffered<Driver,N>`) y orquestación de copper por escena (`CopperPlan`) |
| `docs/guides/roadmap/BITACORA_SCROLL_TILES.md` | bitácora | Estado y lecciones del scroll por tiles (fix de `BPLCON1` de la 101, multi-modo, optimizaciones O(n²)→O(n), cifras de fps) |
| `docs/guides/roadmap/ROADMAP_GAME_AI.md` | **vigente (G1–G4 entregados)** | IA de juego: fases G1–G5 y catálogo de técnicas; estado de `eng::ai` en `docs/engine/architecture/GAME_AI_LIBRARY.md` |
| `docs/guides/roadmap/ROADMAP_BOARD_GAMES.md` | **vigente (B0–B6 hechos; backends de disco/packers y B7 pendientes)** | Motores de tablero (`eng::board`): ajedrez completo (núcleo, reglas, búsqueda con TT/null-move/Multi-PV, evaluación, conocimiento, NLG y E/S de fichero del PC) verificado por HOST-137…151; `games/100_chess` corre en emulador (build→run→analyze). Faltan el trackloader Amiga, packers de tablas/patrones y Go 9×9. Diseño en `docs/engine/architecture/BOARD_GAME_AI.md` y concurrencia en `docs/engine/architecture/PARALLEL_AND_THREADS.md` |

## Estado real del engine y las demos (2026-09)

- **Scroll**: corkscrew 8-way X-Limited (`XLimitedPlayfield` + `ScrollEngine` +
  `ScrollSink`), tiles interleaved de 320 px, wrap toroidal, anillo vertical. **En refactor**:
  el modelo objetivo separa algoritmo/superficie/composición/máquina
  (`docs/engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md`) con plan en
  `docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`.
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
- **Matemática y assets (import demoscene, 2026-09)**: `eng/retro/lib2d.hpp`
  (lib2d: matrices 2×2 4.12 + `clip_line`/`clip_polygon`) y `eng/platform/amiga/gfx3d.hpp`
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
- **IA de juego (`eng::ai`) — G1–G4 entregados** y verificados por test host: planificación
  GOAP (HOST-107), decisión (`AgentFsm`, utility, behavior tree y blackboard; HOST-110…113),
  navegación (flow field, waypoints con A* y navmesh lite con string-pulling; HOST-114/116/118)
  y movimiento/percepción (steering + mapa de influencia y memoria; HOST-115/117). Falta la
  verificación por demo y G5 (diseño). Detalle: `docs/engine/architecture/GAME_AI_LIBRARY.md`;
  plan y catálogo: `docs/guides/roadmap/ROADMAP_GAME_AI.md`.
- **Ecosistema vivo (`eng::sim`) — modelo base entregado** y verificado por test host:
  criatura (necesidades con peligro ambiental genérico, personalidad, relaciones con
  **afecto dirigido**, trackers, mente de doce emociones, conocimiento aprendido/compartido,
  genoma y jerarquía), mundo con LOD abstracto/realizado, tick escalonado, grafo de
  habitaciones, migración al refugio, **ciclo de vida y reproducción** con gestación,
  **objetos materiales** (inventario + ejecución del plan), **economía/reputación**,
  **terreno y clima** por región (representación algorítmica del mundo + `astar`),
  **rumores/memoria de grupo**, sociedad/enjambre (HOST-152…154, HOST-156…174),
  **cuerpo procedural** expresivo (HOST-156), **percepción multimodal** (visión/oído/olfato/
  tacto/gusto/temperatura; HOST-160), **atención** y **sentidos por genética** (HOST-162),
  **memoria de corto y largo plazo** con consolidación, **memoria espacial/rutas macro**,
  **mapa mental aplicado al movimiento** (corto y fino; HOST-161/163/164/166), **clima con
  frentes** (HOST-165), **terreno dinámico** (HOST-167), **trueque/reputación** (HOST-168),
  **biomas/ecosistemas** (HOST-169), **lenguaje/gestos** atados a emoción y jerarquía
  (HOST-170), **cultura/rituales** y **manadas** (HOST-171/172), un **laboratorio de
  escenarios** con digesto y ajustes (HOST-173; bitácora en
  `docs/debugging/SIM_ECOSYSTEM_SCENARIOS.md`) y **jugador simulado con LOD** y aforo por
  región (HOST-174), y **planificación GOAP integrada** con dominio de construcción
  (HOST-155), con gate de codegen 68000. Reutiliza `eng::ai` (utility, percepción,
  navegación, steering, GOAP). Falta **consumidor real en `games/`** (demo Amiga con
  render) y las líneas de crecimiento (percepción imperfecta, tácticas de manada).
  Detalle: `docs/engine/architecture/SIM_ECOSYSTEM.md`.

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
    «Demos atractivas» de `docs/guides/methodology/DEMO_VISUAL_DEBUG.md`.

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

## Soporte de 256 px de scroll vía `linear_display` (decisión abierta)

Objetivo: poder usar un **viewport de scroll de 256 px** (pantalla completa, sin gastar 48 px en un
HUD) manteniendo el corkscrew 8-way.

Motivo: el comparador de 8 bits del Copper impide el **split móvil** en líneas raster ≥256 y el
corte se recorta a 255. Es límite de hardware común a OCS/ECS/AGA, ratificado por IA externa
(`docs/guides/roadmap/CONSULTA-SPLIT-208.md`).

Vía: `linear_display` (espejo vertical del bucle). Ya está implementado y es el defecto de la demo
107 (`K_LINEAR=1`), verificado con 0/119 pares desincronizados.

Coste conocido:
- **Blitter**: cada celda entrante se blittea 2× (bucle + espejo) → ~2× `blit_jobs`/`blit_words` en
  los cruces. El coste es proporcional al salto, no al tamaño de pantalla.
- **Chip RAM**: `display_height × row_bytes × planes` bytes extra en el campo espejado (anillo 288,
  44 B/fila, 6 planos ≈ 74 KB; menos con 3-5 planos).
- **DPF**: con `dual_linear_field` el sobrecoste recae solo en el campo lineal.

Pendiente:
1. Medir fps reales de un corkscrew a 256 px con `linear_display` en una demo (gate 50 fps) y fijar
   el presupuesto de Chip RAM del modo.
2. Decidir si se promueve a modo estándar de la 107/202 o se deja como variante paramétrica.

Alternativa más barata si se acepta menos área: **campo corto + HUD** (208 de scroll + 48 de HUD),
canónico desde la 201.

## F6 — Variante rápida «Sonic» (medición E1, 2026-09)

Objetivo: pasos >2 px/frame (5-6 px, estilo Sonic) garantizando que la fila/
columna entrante se dibuja siempre a tiempo, y tiles de 32×32.

> **Diseño general**: la política de relleno y guarda para scroll rápido está en
> `docs/engine/architecture/FAST_SCROLL.md` (`Progressive` | `TileBurst<N>` | `StripPrerender<C>`,
> `guard_tiles`, dirección laceda a frontera de tile, tear-free). Esta sección conserva la
> medición E1 que la motiva y las opciones de tiles 32×32.

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
- **F. Streaming de contenido y carga desde disco**: llevar `StreamingWorldMap`/`ChunkCache` a un
  `Loader` real que lea del disquete en segundo plano mientras se dibuja (patrón de loaders de
  juego). Formato de mundo: `docs/engine/architecture/WORLD_FORMAT.md`; diseño del Loader:
  `docs/engine/architecture/STREAMING_LOADER.md`; base de hardware (trackdisk vs. trackloader vs.
  HD): `docs/reference/amiga/techniques/trackloading.md`; pasos concretos en Fase 7 de
  `docs/guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md`.

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
- ✅ Parte 3: demo **`112_xlimited_robocod`** — XYLimited de **5 planos** con el
  plano 4 de **fondo geométrico con parallax RoboCod** (`parallax_plane=4`,
  `parallax_div=2`: su `BPLxPT` avanza a la mitad), paleta de 32 índices mapeada a
  16 tonos (bit 4 = capa de fondo). El patrón son "tiles" geométricos procedurales
  (`XLimitedPlayfield::fill_parallax_pattern`), sustituibles por arte después.
  `analyze` OK.
  Ajuste visual (2026-09): **XYLimited 8-way** con movimiento en X e Y (rebote sobre
  el área extra), **X `Finite`** (el patrón de fondo no choca con el *unroll* del
  X-Limited largo), **FG de plataformas (~15%)** sobre **85% de fondo**, paleta con
  el **truco RoboCod** (`palette[c]==palette[c+16]` para que el plano de fondo no
  tiña el FG; solo el índice 0/16 pasa de negro a patrón). El fondo son **bandas
  diagonales** geométricas procedurales con `parallax_div=2`. `analyze` OK.
  **Rediseño final (2026-09)**: el plano de fondo en un **mismo playfield no
  funciona** — el blit del FG escribe el 5.º plano a 0 y **borra el fondo** en las
  filas que entran al scrollear (el Blitter interleaved no salta un plano; haría
  falta blit por plano). Solución limpia: **DPF de 2 capas** (`112` reescrita, 3+3):
  FG = plataformas (PF1, delante) + BG = patrón geométrico (PF2) a **mitad de
  velocidad** (parallax); cada capa con su bitmap, sin borrados. Ollama lo valida
  (“plataformas naranjas sobre fondo de bandas diagonales azules, limpio”).
  **Raster colors HECHAS**: el color del patrón de fondo (`COLOR14`) cambia por banda
  de raster a **tonos pastel** que contrastan con el negro (compositor dual: `ColorZone`
  + `linear_display`, sin split), más un **motivo de puntos** en los tiles (se ve el
  tileado). Pendiente: **tileset artístico** y más motivos.- ⏳ Transversal: **tiles 64×64** en `BlocksBitmap`/pipeline (128 tiles ya se usa);
  matriz de memoria Chip por modo DPF.
- ⏳ Fino: `y_mode` `Finite` (Y corto con scroll); pulido de objetos de 110/111.

## Subsistema de gráficos poligonales / wireframe (propuesta 2026-09)

**Idea**: convertir el importe 3D del demoscene (`flatshade-convex`, `wireframe`, `flatshade`, `stencil3d`, `texobj`, `blurred3d`, `starfox`, `anim-polygons`, `dna3d`) en un **subsistema de render poligonal/wireframe** reutilizable, no en demos sueltas. La base ya está en el repo:

- **Modelo y matemática**: `eng/platform/amiga/object3d.hpp` (malla `obj2c` + `Object3D`, port 1:1 de lib3d; HOST-014) y `eng/core/mesh3d.hpp` (`MeshView`, `mesh_transform`, `mesh_painter_order`; HOST-013); `lib2d`/`math3d` (4.12, `div_wide`/`normfx`; HOST-010/011).
- **Primitivas Blitter**: `blitter_line` (OR), `blitter_line_eor` (ONEDOT+EOR, con `d_base`), `blitter_area_fill` (FILL_XOR), `blitter_fill_polygon` (máscara+cookie-cut), `fill_triangles_blitter`.
- **Técnica canónica**: `docs/reference/amiga/techniques/blitter-line-subpixel-fill.md` §3 (receta del polígono relleno; truco `BLTDPTR`=base, `BLTSIZE` altura 0).
- **Demos cabecera**: 077/078 (`math3d` cube/solid), 079 (`wireframe`), 116 (`flatshade-convex` fiel).

**Diseño propuesto** (capas análogas a `RastPort`, sin exponer registros/planos):

```text
  Object3D/MeshView  ->  MeshRenderer  ->  PolygonSurface (Blitter)
     (datos 3D)         culling + transform    draw_line / fill_polygon
                        + visibilidad          (oculta planos, d_base,
                                                row_bytes, minterms)
```

- **`PolygonSurface`** (contexto de dibujo): expone `draw_line(p0,p1,color)` y `fill_polygon(pts,color)` eligiendo internamente la ruta (contorno `ONEDOT`+EOR + `area fill` XOR, o máscara + cookie-cut) y aplicando el truco `BLTDPTR`. Firma sin punteros ni registros (regla de API del engine); funciona igual en 4/5/6 planos y EHB/DPF.
- **`MeshRenderer`**: culling (`face_visible`/`UpdateFaceVisibility*`), transform (`transform_vertices`), visibilidad de aristas (simple o la convexa por XOR) y selección de ruta (alambre vs relleno por luz de cara).
- **Reutilización**: 077/078 ya calculan culling+orden (`mesh_painter_order`); el subsistema unifica esas rutas con las del import 3D.

**Hitos**: (1) extraer de 116 un `PolygonSurface` reutilizable en `engine/` + test host (contrato de registros y paridad); (2) `MeshRenderer` con ruta alambre (079) y relleno (116); (3) importar `flatshade` (no-convexo) y `stencil3d` sobre él (ambos usan `BLTDPTR`=base); (4) rasterizador **sub-píxel** (acumulador 12.4, §2 de la ficha) para aristas/polígonos sin *snap* a píxel.

**Estado**: **base del subsistema hecha** (Hitos 1–2 + hook + alambre). Reutilizando lo existente, sin clases nuevas:
- **`eng::field::Surface::fill_polygon`** (polígono convexo): **recorta** con `eng::retro::clip_polygon` contra el clip y **delega** en **`Playfield::fill_polygon`**, un **hook virtual** cuya implementación por defecto es el relleno por scanline CPU. Un playfield de Amiga puede sobrescribirlo para rellenar por **Blitter** (área fill + truco `BLTDPTR` de 116) sin que el llamador cambie. Tests **HOST-045**.
- **`eng::graphics::mesh_renderer.hpp`**: compone el pipeline 3D→2D (`mesh_transform` + `mesh_painter_order` + `project_perspective` + `Surface`), con **ruta rellena** (`mesh_render_filled<ColorFn>`, color por cara) y **ruta alambre** (`mesh_render_wire`, aristas por `draw_line`). Buffers del llamador, sin heap, genérico por modo. Test **HOST-046**.

**Estado hardware (validado en emulador)**: el pipeline de malla **funciona en 68k** — la demo cabecera (cubo en 110) dibuja **2 caras** (`g_eng_run_status.detail` bits 23..20, resuelto por `.map`), confirmando malla `constexpr`/rodata, `mesh_transform`/`project_perspective`, culling y el relleno por scanline. El **bloqueo era el coste del relleno CPU del hook**: `write_pixel` cuesta **~1500 ciclos/píxel** en el emulador (un triángulo de ~6 400 px ≈ 10,7 M ciclos/frame → 0,66 fps; el cubo ≈ 21,4 M → 0,33 fps), frente a ~479k del 110 limpio.

**Override Amiga del hook por Blitter — HECHO (API NO VERIFICADA por demo)**: el relleno de polígonos se delega al Blitter sin que el llamador cambie.
- **Seam backend-agnóstico** `eng::field::PolygonFillSink` (`playfield.hpp`): `Playfield::fill_polygon` llama al sink si está instalado (geometría planar por `plane_base`/`plane_stride`/`row_stride`, que cubre contiguo e interleaved) y cae al scanline CPU si no. Test **HOST-062**.
- **Motor** `MinimalBackend::blitter_fill_polygon_strided` (`amiga_minimal.hpp`): máscara 1 bit + contorno `ONEDOT` + area fill inclusivo + cookie-cut a cada plano, con strides explícitos; `blit_mask_to_plane` acepta `dst_row_stride` (variante interleaved). El `blitter_fill_polygon` contiguo de 116 delega en él.
- **Puente de plataforma** `eng::amiga::PolygonFillService` (`platform/amiga/polygon_fill.hpp`): envuelve backend + máscara Chip en el sink; la escena instala `scene.canvas_fg().set_polygon_fill_sink(...)`.
- **Prueba en hardware (retirada)**: un cubo sólido (`mesh_render_filled`) se rellenó por Blitter sobre el lienzo FG interleaved del DPF, con el borrado del recuadro también por Blitter. Se descartó como demo por coste: ~66 blits serializados/frame (cada blit paga arranque + ~57 ciclos por escritura a registro custom y espera al anterior) → el frame no cabe y el redibujado sobre el lienzo de un solo buffer produce flicker.

Pendiente: una demo que ejercite el relleno poligonal por frame exige **abaratarla** (menos blits por cara) y **doble buffer del lienzo FG** (`DoubleBufferScrollPlayfield` es la base para scroll; falta el equivalente para el FG del DPF). Hasta entonces el sink y la ruta Amiga quedan respaldados solo por **HOST-062** (NO VERIFICADA por hardware) e importar `flatshade`/`stencil3d` sobre él no es prioritario. Nota: el Blitter de `amiga_minimal` cobra ~57 ciclos por escritura a registro custom y cada blit espera al anterior, así que el número de blits por cara domina el coste.

**Capa de bobs/personajes**: ver opción A del roadmap general (capa de objetos en el FG lineal DPF, 110/111 ya a medio pulir).
