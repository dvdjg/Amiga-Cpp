# Demos Amiga

Demos del engine para **Amiga OCS/ECS (A500)**. Convención de nombres y
estructura: `docs/STRUCTURE.md` §4. Clasificación por concepto:

- **Fundamentos (`0xx`)**: toolchain/boilerplate, Chip RAM, Copper, paletas,
  bobs/Blitter.
  - `000_toolchain_cpp23` — herramienta/toolchain, hola mundo con READY.
  - `010_chip_slow_memory` — Chip vs Fast RAM.
  - `020_copper_basic` — copperlist básica.
  - `030_ehb_palette_zones` — paletas EHB por zonas.
  - `040_palette_cycle_effect` — ciclo de paleta.
  - `050_blitter_bobs`, `051_blitter_shifted_bobs`, `052_tile_staging_blits` — Blitter y bobs.
- **Sprites, Copper e input (`053-056`)**:
  - `053_sprite_multiplex`, `054_sprite_allocator` — multiplexado y reparto de sprites.
  - `055_copper_rainbow` — rainbow de Copper.
  - `056_input_aggregator` — joystick/ratón/teclado (poll + decode); tests HOST-004/006/007.
- **C2P y fuego (`061-063`)**:
  - `061_c2p_chunky_4bpl`, `062_fire_c2p`, `063_fire_cpp_vs_asm` — conversión chunky→planar y efecto fuego.
- **Audio (`057-076`)** — serie incremental; diseño en `docs/engine/architecture/GAME_AUDIO.md` y `AUDIO_MIXER.md`, y pipeline de muestras en `tools/audio/prep-sample.ts`:
  - `057_audio_mixer` — plan de audio (SFX + música) en el engine.
  - `058_sfx_mixer` — Audio Mixer 3.7 nativo (efectos por AUD0).
  - `059_music_player`, `060_music_pt` — reproductores P61 y ptplayer.
  - `061_audio_system` — fachada `AudioSystem` (SFX + música).
  - `062_game_audio` — capa de juego (banco, política de voces, ducking); test HOST-008.
  - `063_harmony` — 3 voces de música por ptplayer.
  - `064_audio_debug` — seno directo por AUD0 (base de la depuración de audio).
  - `065_single_voice` — una voz por ptplayer con la codificación de nota correcta.
  - `066_polyphony` — 3 voces independientes por ptplayer.
  - `067_mixer_melody` — melodía pre-renderizada por 1 voz del mixer.
  - `068_mixer_ref` — test espejo de `CMixer.c` (API ASM cruda; aisló el bug del buffer de plugins).
  - `069_mixer_two_voices`, `070_mixer_three_voices`, `071_mixer_four_voices` — 2/3/4 voces pre-renderizadas en el mixer.
  - `072_sample_channel` — sample real de ST-xx por AUD0 (DMA de Paula directo).
  - `073_sample_mixer` — el mismo sample por el mixer (`MixCh0`).
  - `074_mixer_drums` — caja de ritmos con 4 samples de percusión reales.
  - `075_mixer_tones` — tonos puros 300/600/1200/2400 Hz activados por un contador binario de 4 bits (diagnóstico; destapó el desbordamiento de `f << 22`).
  - `076_mixer_sample_channels` — un sample real en loop por cada voz del mixer, activado por el contador de 4 bits (verificación de las 4 voces).
- **Matemática 2D/3D (`077`)** — ports de `lib2d`/`lib3d` de demoscene (tests host HOST-010/011/013; índice `docs/demos/effects/LIBRARIES-CPP23-IMPORT-ROADMAP.md` §5):
  - `077_math3d_cube` — cubo 3D en alambre con `math3d` (rotación 4.12) + `mesh3d` (`mesh_transform`, back-face culling y orden painter) sobre EHB. Valida la matemática entera (sin soft-float) en hardware; dibujo CPU durante el vblank.
  - `078_math3d_solid` — el mismo cubo con las caras **rellenas** (rasterizado de triángulos por scanline, relleno por tramos de byte/word) en orden lejos→cerca, con borrado del frame por **Blitter** (`FramePlan`+`CopyRect`). La malla se carga desde un **blob UAF-R** (`uaf-pack --mesh` → `src/prebuild.sh` → incbin → `Blob`+`MeshAssetView`), cerrando el ciclo exportador→runtime en 3D. El relleno por defecto es la ruta **CPU** (tramos de byte/word, verificada); incluye dos rutas **Blitter** tras `-DK_FILL_BLITTER=1` (`MinimalBackend::fill_triangles_blitter` = line+area-fill+cookie-cut, y `blit_fill_from_mask` = máscara CPU + cookie-cut Blitter). Ambas dibujan pero aún salen **rayadas** (WIP: alineación/carry de los canales A/C-D del Blitter).
- **Porte 1:1 del demoscene (`079`)** — método en `docs/guides/roadmap/PORT_PROMPT_1A1.md`:
  - `079_wireframe` — **PORTE 1:1** de `effects/wireframe/wireframe.c`: objeto `pilka` (`obj2c`) en alambre con la **línea por Blitter** (`MinimalBackend::blitter_line`, secuencia idéntica a `DrawObject`) y el modelo `Object3D` de `lib3d` portado en `eng/core/object3d.hpp` (test HOST-014). Versión B: display 320×256×4 del engine (el original era 256×256×4) y sin doble buffer por rotación de planos; la geometría y el algoritmo son los del original. Ver `docs/demos/effects/WIREFRAME_PORT_PLAN.md`.
  - `080_fire_rgb` — **PORTE 1:1** de `effects/fire-rgb/fire-rgb.c`: simulación de fuego en asm (`support/fire_loop.s`) 80×64 → chunky → C2P por Blitter (13 fases) → display **HAM6** 320×256 con **cuadruplicado de líneas** por Copper (driver `eng::graphics::drivers::HamScene`, test HOST-016). El C2P se solapa en el hueco de VBlank. Ver `docs/demos/effects/FIRE_RGB_PORT_PLAN.md`.
  - `081_background_tasks` — **tareas de fondo cooperativas** (`eng::task::BackgroundQueue`, test HOST-017): un proceso pesado (barra progresiva) avanza mientras el bucle principal sigue pulsando el fondo y trazando una línea por Blitter (cuyas esperas también drenan el fondo); la tarea adapta su carga al barrido del CRT (`vpos`). Ver `docs/engine/architecture/BACKGROUND_TASKS.md`.
- **Scroll y tile fields (`1xx`)**:
  - `100_virtual_tile_scene_scroll` — escena virtual con scroll.
  - `101_ehb_tile_scroll_driver`, `102_tile_scroll_dualpf`, `103_tile_scroll_ring`,
    `104_tile_scroll_ring_dualpf`, `105_tile_scroll_xyunlimited_dualpf`,
    `106_tile_field_showcase`, `107_xlimited_corkscrew` — drivers de scroll y tiles.
- **Escenas con pipeline de assets (`2xx`)**:
  - `201_ehb_map` — mapa real EHB X-Limited con el pipeline completo.
  - `202_xlimited_dpf` — dual playfield parallax 2:1.

Cada demo documenta en su `README.md` invariantes, comandos y validación.
Build/run/analyze desde `demos/amiga/`:
```
bash ./tools/build/build-demo.sh demos/amiga/107_xlimited_corkscrew --debug --clean
bash ./tools/run/run-demo.sh demos/amiga/107_xlimited_corkscrew
bash ./tools/analyze/analyze-demo.sh demos/amiga/107_xlimited_corkscrew
```