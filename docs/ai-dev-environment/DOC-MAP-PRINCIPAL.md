# Doc-map principal (IA → documentación)

**Archivo de navegación obligatorio para la IA antes de tocar el engine o las demos.**

Este documento es la puerta de entrada única: ante cualquier tarea de desarrollo,
efecto o consulta de hardware, la IA debe **leer primero este mapa** para saber
qué documentación consultar, qué demos/APIs ya existen y qué reglas aplicar.
Cuando termine la lectura contextual, puede abrir solo los enlaces que necesite.

> Regla de oro: **buscar antes de crear.** Si la técnica, el efecto o la
> referencia ya está documentado, consiste en *reutilizar/extender/componer*,
> nunca en duplicar. Solo cuando no exista nada equivalente se crea contenido
> nuevo, y siempre en el sitio canónico (ver [STRUCTURE.md](../STRUCTURE.md)).

---

## 1. Protocolo previo a cualquier tarea

1. Si es la **primera vez** en el repo en esta sesión, leer `AGENTS.md` y este
   doc-map.
2. Identificar el **tipo de tarea** en la tabla §3 y abrir los documentos de su
   fila antes de escribir código.
3. Si la tarea es **un efecto** o **una técnica**, consultar §4 (efecto → demo/API)
   y comprobar si ya hay cobertura (estado de la fila en el coverage index).
4. Si la tarea requiere **referencia oficial de hardware**, ir a §5 (AHRM 3.ª edición + fuentes autoritativas).
5. Si la tarea viene de un **repositorio o directorio externo** (nuevo efecto,
   referencia, demoscene-repo, etc.), aplicar el protocolo de ingesta de §6.
6. **Después** de implementar: documentar (sitio canónico), enlazar aquí y en
   `docs/README.md`, y registrar estado en el coverage/roadmap correspondiente.
   Seguir la regla de evidencia de `AGENTS.md` (build → run → analyze).

---

## 2. Lectura contextual mínima (según profundidad)

| Si necesito... | Leer |
|---|---|
| Contexto global del proyecto | [docs/README.md](../README.md), [CONTINUATION_CONTEXT.md](../CONTINUATION_CONTEXT.md) |
| Estructura del repo (dónde va cada cosa) | [docs/STRUCTURE.md](../STRUCTURE.md) |
| Capas de plataforma y contrato de backend (dominio ↔ chipset ↔ backend) | [PLATFORM_LAYERS.md](../engine/architecture/PLATFORM_LAYERS.md) |
| Política de cabeceras (header-only vs `.cpp`) | [HEADER_POLICY.md](../engine/architecture/HEADER_POLICY.md) |
| Taxonomía de tests (plataforma/nivel/categoría) | [testing/TAXONOMY.md](../testing/TAXONOMY.md) |
| Arquitectura del engine C++ | [docs/engine/architecture/](../engine/README.md) y sus subcarpetas |
| Modelo objetivo playfield/scroll (contrato de refactor) | [PLAYFIELD_SCROLL_ARCHITECTURE.md](../engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md) + [REFACTOR_PLAYFIELD_SCROLL.md](../guides/roadmap/REFACTOR_PLAYFIELD_SCROLL.md) |
| API pública (la app no ve hardware) | [PUBLIC_API.md](../engine/architecture/PUBLIC_API.md) + [SCENE_AND_RESOURCES.md](../engine/architecture/SCENE_AND_RESOURCES.md) (escena retenida y ocupación de recursos) |
| Contenido (tiles/mundo disperso/sprites/audio) | [CONTENT_AND_TILEMAP.md](../engine/architecture/CONTENT_AND_TILEMAP.md) |
| Objetos/actores (sprite vs BOB vs CPU, transparencia, fondos, Copper por objeto) | [OBJECT_SYSTEM.md](../engine/architecture/OBJECT_SYSTEM.md) + [VISUAL_EFFECT_SPRITE_DESIGN.md](../engine/architecture/VISUAL_EFFECT_SPRITE_DESIGN.md) (Visual/CopperIntent/SpriteTemplate) |
| Streaming / carga en segundo plano | [CONTENT_AND_TILEMAP.md](../engine/architecture/CONTENT_AND_TILEMAP.md) §2 (chunks/prefetch), [WORLD_FORMAT.md](../engine/architecture/WORLD_FORMAT.md) (formato de mundo), [STREAMING_LOADER.md](../engine/architecture/STREAMING_LOADER.md) (Loader: RAM/trackdisk/trackloader), [trackloading.md](../reference/amiga/techniques/trackloading.md) (carga desde disquete/HD) |
| Estilo/restricciones del engine | [CODING_STYLE.md](../engine/architecture/CODING_STYLE.md), [HARDWARE_AND_ROM_KERNEL_POLICY.md](../engine/architecture/HARDWARE_AND_ROM_KERNEL_POLICY.md) |
| **Cómo se hacen las cosas** (patrones de escalares/mallas/raster + metodología) | [PATRONES_Y_PROCESO.md](../engine/architecture/PATRONES_Y_PROCESO.md) |
| Contrato de bajo nivel Amiga (contrato técnico) | [amiga-lowlevel-agent-prompt.md](../guides/methodology/amiga-lowlevel-agent-prompt.md) y [amiga-lowlevel-technique-contract-template.md](../guides/methodology/amiga-lowlevel-technique-contract-template.md) |
| Bucles de entrada/backend | [engine.hpp](../../engine/include/eng/engine.hpp), `amiga_minimal.cpp`, [MINI_OS_MESSAGE_LOOP.md](../engine/architecture/MINI_OS_MESSAGE_LOOP.md) (capa de mensajes) |
| Build/run/analyze | [BUILD_AND_RUN.md](../build/BUILD_AND_RUN.md) |
| Depuración WinUAE/MCP | [debug-winuae-v2-guide.md](../debugging/system/debug-winuae-v2-guide.md) |
| Sistema de depuración (índice) | [debugging/system/README.md](../debugging/system/README.md) |
| **¿Ya se investigó?** Bloqueos y rarezas conocidas | [debugging/investigaciones/README.md](../debugging/investigaciones/README.md) + [reference/emulators/README.md](../reference/emulators/README.md) (fuente del emulador, `fichero:línea`) |
| Evidencia / visual | [session-evidence.md](session-evidence.md), [testing/](../testing/README.md) |

---

## 3. Tabla de tareas → documentación

| Tipo de tarea | Documentos a consultar primero | Demos/APIs de referencia |
|---|---|---|
| **Organizar el engine / añadir plataforma / mover tests** | [PLAN_ORGANIZACION_ENGINE.md](../guides/roadmap/PLAN_ORGANIZACION_ENGINE.md) (plan), [PLATFORM_LAYERS.md](../engine/architecture/PLATFORM_LAYERS.md) (anillos y contrato), [HEADER_POLICY.md](../engine/architecture/HEADER_POLICY.md) (header-only vs `.cpp`), [testing/TAXONOMY.md](../testing/TAXONOMY.md) (tests), [ENGINE_STRUCTURE_REVIEW.md](../engine/architecture/ENGINE_STRUCTURE_REVIEW.md) (decisiones D9–D12) | gates `tools/check/{platform-boundaries,engine-tree,test-numbering}.mjs` |
| **Depurar algo ya investigado / conocer rarezas y bloqueos** | [docs/debugging/README.md](../debugging/README.md) (índice de investigaciones: abiertas vs cerradas), [docs/reference/emulators/README.md](../reference/emulators/README.md) (rarezas del emulador por tema, con `fichero:línea`) | cada doc de `docs/debugging/` cita la demo/test afectado |
| **Integrar/portar código de terceros que falla** | [LECCION-CONTEXTO-DE-LA-FUENTE.md](../guides/methodology/LECCION-CONTEXTO-DE-LA-FUENTE.md) (el contexto de la fuente primero: repo de confianza/README/ejemplo; línea base que funcione antes de tocar), `AGENTS.md` §1.7 | el ejemplo del propio repo de origen |
| **Nuevo efecto demoscene** | §4 de este mapa, [DEMOSCENE_EFFECT_REPLICATION_POLICY.md](../demos/effects/DEMOSCENE_EFFECT_REPLICATION_POLICY.md), [demoscene-repo-coverage-index.md](../demos/effects/demoscene-repo-coverage-index.md), [demoscene-repo-import-roadmap.md](../demos/effects/demoscene-repo-import-roadmap.md), §5 para registros | `engine/` y `demos/` de la técnica base más cercana |
| **Scroll / tiles por playfield** | [PLAYFIELD_SCROLL_ARCHITECTURE.md](../engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md) (modelo objetivo: algoritmo/superficie/composición/máquina), [FAST_SCROLL.md](../engine/architecture/FAST_SCROLL.md) (política de scroll rápido Sonic/Superfrog), [XYLIMITED_ALGORITMO_GENERICO.md](../engine/architecture/XYLIMITED_ALGORITMO_GENERICO.md) (algoritmo + invariantes), [robocod-layered-scroll.md](../reference/amiga/techniques/robocod-layered-scroll.md), §5 (BPLCON1/BPLxPT) | `engine/include/eng/graphics/playfield_scroll.hpp` (convenciones canónicas de `BPLCON1` fine delay / `DDFSTRT` adelantado / coarse del puntero), `engine/include/eng/api/effects.hpp` (`effects::FineScroll`), `engine/include/eng/field/xlimited.hpp`, demos 100–107, 110–112, 201–202 |
| **Streaming de contenido / carga desde disco** | [WORLD_FORMAT.md](../engine/architecture/WORLD_FORMAT.md) (formato de mundo), [STREAMING_LOADER.md](../engine/architecture/STREAMING_LOADER.md) (diseño del Loader), [trackloading.md](../reference/amiga/techniques/trackloading.md) (carga en segundo plano desde disquete/HD), [CONTENT_AND_TILEMAP.md](../engine/architecture/CONTENT_AND_TILEMAP.md) §2 (chunks/prefetch), [BACKGROUND_TASKS.md](../engine/architecture/BACKGROUND_TASKS.md) | `engine/include/eng/field/chunk_cache.hpp`, `streaming_map.hpp`; demo 111 |
| **Copper / paleta / per-line** | [amiga-a500-dma-copper-state-rules.md](../reference/amiga/hardware/amiga-a500-dma-copper-state-rules.md), técnicas `copper-chunky.md`, `dx39...analysis.md` | `engine/.../copper/`, demos 020, 030, 040 |
| **Carretera/suelo por raster** (perspectiva, curvas, rasantes; `BPLxPT` por línea) | [copper-road-rasters.md](../reference/amiga/techniques/copper-road-rasters.md) (técnica + enlaces), [ROADMAP_RASTER_ROAD.md](../guides/roadmap/ROADMAP_RASTER_ROAD.md) (plan), [PLAYFIELD_SCROLL_ARCHITECTURE.md](../engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md) | `copper::Scheduler`/`PatchHandle`/`DoubleBuffer`; demos con split de raster (107/110/111/112); HOST-R0…R7 |
| **Zoom hardware** (vertical por `BPLxMOD`, horizontal por `BPLCON1`/`$102`) | [hardware-zoom.md](../reference/amiga/techniques/hardware-zoom.md) (técnica + enlaces), [ROADMAP_HW_ZOOM.md](../guides/roadmap/ROADMAP_HW_ZOOM.md) (plan) | `compose.hpp::row_repeat`, `Scheduler::wait_position`, `FineScroll`; rotozoom software (`rotozoom.hpp`, HOST-132); HOST-Z0…Z5 |
| **Blitter / BOBs / minterms / líneas** | [DEMOSCENE_REPO_INDEX.md](../demos/effects/DEMOSCENE_REPO_INDEX.md) (blitter interleaved), técnicas `dual-playfield-fastbobs.md`, `blitter-line-subpixel-fill.md` (líneas + **receta de polígono relleno**: contorno `ONEDOT`+EOR + `area fill` XOR, truco `BLTDPTR`=base y `BLTSIZE` altura 0), `blitter-memcpy.md` (copia lineal por Blitter), [ROADMAP_BLITTER_COPPER.md](../guides/roadmap/ROADMAP_BLITTER_COPPER.md) (plan Copper↔Blitter), AHRM cap. 6 | demos 050, 051, 052, 079, 116, 200s |
| **Dibujo CPU/Blitter tras `Surface`** (líneas, relleno, blit, ops lógicas) | [RASTER.md](../engine/architecture/RASTER.md) | HOST-212, demos 077/078 |
| **Dual playfield / prioridad / parallax por capas** | `dual-layer.md`, `robocod-layered-scroll.md`, DPF_MIXTO_SPLIT_LINEAL.md, dx39-layers analysis | demos 102, 104, 105, 107, 110, 111, 112, 202 |
| **Sprites / overlays** | `sprite-layer.md`, AHRM cap. 4 | — (ver coverage) |
| **Audio / Paula** | [GAME_AUDIO.md](../engine/architecture/GAME_AUDIO.md) (capa de juego, modos y canales), [AUDIO_MIXER.md](../engine/architecture/AUDIO_MIXER.md) (mixer Photon), [MUSIC_PLAYER.md](../engine/architecture/MUSIC_PLAYER.md) (P61/pt/OctaMED), [AUDIO_STREAMING.md](../engine/architecture/AUDIO_STREAMING.md) (streaming desde disquete), [ROADMAP_AUDIO.md](../guides/roadmap/ROADMAP_AUDIO.md) (plan), [audio-stream-irq-rate.md](../debugging/investigaciones/audio-stream-irq-rate.md) + [winuae/audio-irq.md](../reference/emulators/winuae/audio-irq.md) (IRQ de audio: hallazgos y bloqueo abierto de A5), `audio-mixing.md`, AHRM cap. 5 | `engine/include/eng/audio/`; demos 058–062; HOST-008 y 242 |
| **3D fixed-point / wireframe** | DEMOSCENE §lib3d, `blitter-line-subpixel-fill.md` (línea por Blitter), `object3d.hpp`/`mesh3d.hpp` (HOST-014/013), [3D_PHYSICS.md](../engine/architecture/3D_PHYSICS.md) (simulación física y opciones) | demos 077, 078, 079, 116 |
| **Matemáticas genéricas / escalar nuevo** | [MATH_LIBRARY.md](../engine/architecture/MATH_LIBRARY.md) (modelo y reglas), [SCALAR_LIBRARY.md](../engine/architecture/SCALAR_LIBRARY.md) (estructura escalar-independiente, límites y guards de compilación), [EXPRESSION_TEMPLATES.md](../engine/architecture/EXPRESSION_TEMPLATES.md) (árbol de expresión lite y fusión por componente), [MINIFLOAT16.md](../engine/architecture/MINIFLOAT16.md) (escalar de 16 bits para 68000), [OPTIMIZACION_GPP_68000.md](../guides/optimization/OPTIMIZACION_GPP_68000.md) | `engine/include/eng/core/{fixed,linalg,minifloat,minifloat_math,interp,geometry,noise,numeric_traits,expr}.hpp`; demo `083_fbm_noise`; HOST-048/049/052/056/057/058/059/060/207 |
| **Escalar genérico / portar a 32-64 bits** (Fixed de 32 bits, tipos nativos, `bool` vs byte) | [REFACTOR_SCALAR_GENERICO.md](../guides/roadmap/REFACTOR_SCALAR_GENERICO.md) (plan y pruebas), [SCALAR_LIBRARY.md](../engine/architecture/SCALAR_LIBRARY.md) §8 | `engine/include/eng/core/{fixed,fixed_math,numeric_traits,scalar_math,word}.hpp` |
| **Utilidades genéricas / contenedores sin STL** | [TEMPLATE_LIBRARY.md](../engine/architecture/TEMPLATE_LIBRARY.md) (librería `eng::util`: rasgos, bits, algoritmos sobre `Span`, contenedores de capacidad fija), [ROADMAP_UTIL_LIBRARY.md](../guides/roadmap/ROADMAP_UTIL_LIBRARY.md) (plan de crecimiento: stats, color, colisión, texto, grid, broadphase, pathfinding, dsp…) | `engine/include/eng/core/util/*.hpp`; HOST-073…092; FSM/eventos (HOST-108/109), DSU/`SparseSet` (HOST-119/120), bits (HOST-121/123), interner (HOST-124), SAT (HOST-125), grafo (HOST-126), LRU/tareas (HOST-127/128), interval/variant (HOST-129/130) |
| **IA de juego (planificación, decisión, navegación, steering, percepción) y diseño de juego** | [GAME_AI_LIBRARY.md](../engine/architecture/GAME_AI_LIBRARY.md) (estado y taxonomía de `eng::ai`), [ROADMAP_GAME_AI.md](../guides/roadmap/ROADMAP_GAME_AI.md) (catálogo de técnicas y fases, G6 crowd/navegación) | `engine/include/eng/ai/`; HOST-107 (GOAP), HOST-110…113 (decisión), HOST-114…118 (navegación, steering, percepción), HOST-249 (crowd) |
| **Ecosistema vivo (criaturas, necesidades, personalidad, percepción multimodal y atención, mente, memoria corto/largo plazo y espacial/mapa mental, conocimiento, afecto dirigido, jerarquía, genética/enjambre, ciclo de vida, sociedad, objetos/economía y trueque, biomas, terreno dinámico/clima con frentes, rumores, lenguaje/gestos, cultura/rituales, manadas, LOD/jugador con IA y entrada humana, cuerpo procedural, planificación GOAP)** | [SIM_ECOSYSTEM.md](../engine/architecture/SIM_ECOSYSTEM.md) (modelo y planos), [sim-ecosystem-scenarios.md](../debugging/investigaciones/sim-ecosystem-scenarios.md) (laboratorio y ajustes) | `engine/include/eng/sim/`; HOST-152…175 (criatura, mundo/LOD/objetos/planificación, mente/afecto/jerarquía/genética/colonia, GOAP/dominio, cuerpo, objetos/economía, terreno/clima, rumores, sentidos, memoria, atención/genoma, memoria espacial/rutas, mapa mental, frentes, rutas con mapa mental, terreno dinámico, trueque, biomas, comunicación, cultura, manadas, escenarios, jugador/LOD/aforo/entrada); codegen `c_sim_*` |
| **Motores de tablero (ajedrez/Go), footprint 20 kB–1 MB, conocimiento en disquete y explicación NLG** | [BOARD_GAME_AI.md](../engine/architecture/BOARD_GAME_AI.md) (diseño), [ROADMAP_BOARD_GAMES.md](../guides/roadmap/ROADMAP_BOARD_GAMES.md) (plan), [STREAMING_LOADER.md](../engine/architecture/STREAMING_LOADER.md) (E/S de conocimiento) | `engine/include/eng/board/`; `games/100_chess`, `games/101_go` |
| **Motores de naipes (póker), información oculta, baraja, equity y footprint 20 kB–512 kB** | [CARD_GAME_AI.md](../engine/architecture/CARD_GAME_AI.md) (diseño), [ROADMAP_CARD_GAMES.md](../guides/roadmap/ROADMAP_CARD_GAMES.md) (plan) | `engine/include/eng/cards/`; `tools/cards/`; HOST-188…193 |
| **Perfil psicológico de NPC (arquetipos, rasgos, aptitudes, defectos, expresión no verbal/gestos, lectura de tells, evolución de partida, convenciones secretas para el Mus)** | [NPC_PSYCHOLOGY.md](../engine/architecture/NPC_PSYCHOLOGY.md) (diseño y catálogos), [ROADMAP_NPC_PSYCHOLOGY.md](../guides/roadmap/ROADMAP_NPC_PSYCHOLOGY.md) (plan), [SIM_ECOSYSTEM.md](../engine/architecture/SIM_ECOSYSTEM.md) (sustrato) | `engine/include/eng/sim/` (extensión); HOST-199…204; integración `eng/cards/ai/persona_bot.hpp`; consumidor `games/200_holdem` |
| **Generación procedural de mundos (metroidvania: biomas, grafos, gating/solvencia, geometría, mecanismos, superficies, física multi-modo)** | [ROADMAP_PROCEDURAL_WORLDS.md](../guides/roadmap/ROADMAP_PROCEDURAL_WORLDS.md) (plan, W0–W12), [WORLD_FORMAT.md](../engine/architecture/WORLD_FORMAT.md) (contenedor de mundo), [SIM_ECOSYSTEM.md](../engine/architecture/SIM_ECOSYSTEM.md) (biomas/terreno/clima a reutilizar) | `eng/sim/{biome,terrain,climate,world}.hpp`, `eng/core/{noise,random}.hpp`, `eng/core/util/{graph,pathfinding,pool}.hpp`, `eng/field/{tile_map,chunk_cache}.hpp`; HOST-277…284; demo bloque D |
| **Concurrencia / portar a plataformas multinúcleo con hilos** | [PARALLEL_AND_THREADS.md](../engine/architecture/PARALLEL_AND_THREADS.md) (frontera `eng::parallel`) | `engine/include/eng/parallel/parallel.hpp`; HOST-137 |
| **Pipeline tiles/EHB/assets** | [PIPELINE_TILES_EHB.md](../demos/tile-pipeline/PIPELINE_TILES_EHB.md), [REGLAS_PIPELINE_TILES.md](../guides/roadmap/REGLAS_PIPELINE_TILES.md), `tools/amiga-tiles/README.md` | demos 201, 202 |
| **Juego sobre el engine** | [STRUCTURE §9](../STRUCTURE.md), roadmap, técnicas | `games/` |
| **API pública / nueva abstracción** | [PUBLIC_API.md](../engine/architecture/PUBLIC_API.md) (la app no ve hardware; regla §1.1: intuitivo/simple/sin restringir), [PUBLIC_GAME_API.md](../engine/architecture/PUBLIC_GAME_API.md) (API de juego, borrador), [HARDWARE_INVENTORY.md](../engine/architecture/HARDWARE_INVENTORY.md) (inventario de hardware `eng::hw`), [INTERNAL_TYPE_SYSTEM.md](../engine/architecture/INTERNAL_TYPE_SYSTEM.md) (tipos de dominio internos: vistas con tag, unidades fuertes, frontera unsafe), [PLAYFIELD_SCROLL_ARCHITECTURE.md](../engine/architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md), [CODING_STYLE.md](../engine/architecture/CODING_STYLE.md), [ENGINE_STRUCTURE_REVIEW.md](../engine/architecture/ENGINE_STRUCTURE_REVIEW.md) (decisiones de consolidación) | `engine/include/eng/api/api.hpp` (fachada de tipos), `eng/api/game.hpp` (`App`/`Screen`), `eng/hw/info.hpp` (inventario); HOST-231/232/233/234/235; demo 205 |
| **Mini-SO / bucle de mensajes / entrada reactiva** | [MINI_OS_MESSAGE_LOOP.md](../engine/architecture/MINI_OS_MESSAGE_LOOP.md) (núcleo: puerto, prioridad, VBlank latched, despacho), [MINI_OS_INPUT.md](../engine/architecture/MINI_OS_INPUT.md) (registros → mensajes), [MINI_OS_TIME.md](../engine/architecture/MINI_OS_TIME.md) (tiempo/timers), [MINI_OS_IO.md](../engine/architecture/MINI_OS_IO.md) (E/S asíncrona/streaming), [MINI_OS_TASKS.md](../engine/architecture/MINI_OS_TASKS.md) (tareas de fondo con ciclo de vida/corrutinas), [ROADMAP_MINI_OS.md](../guides/roadmap/ROADMAP_MINI_OS.md) (plan), [ROADMAP_WORKBENCH.md](../guides/roadmap/ROADMAP_WORKBENCH.md) (host de mensajes en Workbench: `Wait`/IDCMP/IO), [BACKGROUND_TASKS.md](../engine/architecture/BACKGROUND_TASKS.md) (trabajo diferido), [ENGINE_DESIGN.md](../engine/architecture/ENGINE_DESIGN.md) (bucle del engine), [winuae/keyboard-injection.md](../reference/emulators/winuae/keyboard-injection.md) (inyectar teclado en el emulador + handshake de CIA-A) | `engine/include/eng/os/`, `engine/include/eng/ui/` (objetivo); HOST-219…222 y 236…239; demos 212_message_loop y 209_audio_stream |
| **Caché de assets y código dinámico (recursos)** | [RESOURCE_SYSTEM.md](../engine/architecture/RESOURCE_SYSTEM.md) (caché LRU/prioridad + DynLoader `.englib`), [ROADMAP_RESOURCES.md](../guides/roadmap/ROADMAP_RESOURCES.md) (plan), [MINI_OS_IO.md](../engine/architecture/MINI_OS_IO.md) (VFS), [SCENE_AND_RESOURCES.md](../engine/architecture/SCENE_AND_RESOURCES.md) (ocupación) | `engine/include/eng/res/` (objetivo); HOST-244…248; demo 210_zone_resources |
| **GUI / widgets / ventanas con backing store** | [GUI_LIBRARY.md](../engine/architecture/GUI_LIBRARY.md) (diseño), [ROADMAP_GUI.md](../guides/roadmap/ROADMAP_GUI.md) (plan), [RASTER.md](../engine/architecture/RASTER.md) (dibujo CPU/Blitter), [MINI_OS_MESSAGE_LOOP.md](../engine/architecture/MINI_OS_MESSAGE_LOOP.md) (entrada opcional por mensajes), [ROADMAP_WORKBENCH.md](../guides/roadmap/ROADMAP_WORKBENCH.md) (misma app de widgets sobre engine o Intuition) | `engine/include/eng/ui/` (objetivo); `Surface`/`Font8`/`Font5x7`; HOST-223…230; demos 215_gui_widgets y 300_gui_compositor |
| **Optimización de un path** | §12 de [OPTIMIZACION_GPP_68000.md](../guides/optimization/OPTIMIZACION_GPP_68000.md) (rendimiento, comentarios, port a asm), [METODOLOGIA_PROFILING.md](../guides/optimization/METODOLOGIA_PROFILING.md) (pipeline de medida, trampas, optimizaciones identificadas), [PROFILING_FROM_AGENT.md](../tools/PROFILING_FROM_AGENT.md) (perfilar desde el agente sin VSCode) | `tools/analyze/profile-report.mjs`, `tools/analyze/profile-samples.mjs`, `tools/debug/winuae-profile.mjs`, `tools/debug/profile.mjs` |
| **Defectos del compilador / verificar codegen 68000** (libcalls, instrucciones 68020+/FPU, ICE) | [toolchain/m68k-gcc.md](../reference/toolchain/m68k-gcc.md) (defectos con caso mínimo + re-verificación por versión; §3 riesgo de optimizaciones que ocultan defectos) | `tools/analyze/asm-audit.mjs` (`bad020`, `--strict`), `tools/analyze/codegen-report.mjs`; AGENTS §1.10 |
| **Depurar un bug de visual** | [DEMO_VISUAL_DEBUG.md](../guides/methodology/DEMO_VISUAL_DEBUG.md) (diseño + depuración visual con Ollama/secuencias), [debug-winuae-v2-guide.md](../debugging/system/debug-winuae-v2-guide.md), invariantes microtests, y para arranque/display+doble texto/banda: [debug-demo-arranque-doble-texto-banda.md](../debugging/investigaciones/debug-demo-arranque-doble-texto-banda.md) | — |
| **Nueva referencia/documento externo** | §6 de este mapa, [amiga-authoritative-sources.md](../reference/amiga-authoritative-sources.md) | — |

---

## 4. Efectos demoscene → demo/API/estado

**Índice efecto a efecto (estado local, caso batería y API candidata):**
[demoscene-repo-coverage-index.md](../demos/effects/demoscene-repo-coverage-index.md).

**Lectura recomendada por dominio:**

| Dominio | Efectos del catálogo (01–67) | Docs del repo |
|---|---|---|
| Copper por línea / bandas | 04-plasma, 08-floor, 12-stripes, 50-roller, 53-showpchg | `copper-chunky.md`, `dx39` layers, AHRM cap. 2 |
| Playfield / dual / HAM | 39-layers, 58-tiles8, 59-tiles16, 60-tilezoomer, 61-transparency | `dual-layer.md`, técnicas scrolling |
| Sprites | 13-highway (sprites+playfields) | `sprite-layer.md` |
| Blitter creativo | 11-game-of-life, 67-weave, 14-metaballs | AHRM cap. 6 (minterms/línea) |
| 3D | 06-wireframe, 30-flatshade, 55-stencil3d, 56-texobj, 65-uvmap | `demoscene-repo` §lib3d |
| Audio | 44-playahx, 45-playcinter, 46-playp61, 47-playpt | `audio-mixing.md` |
| Texto / UI | 09-textscroll, 27-credits, 37-gui | Texto vía API: `Surface::draw_text`/`draw_text5` (`eng/field/surface.hpp`, UTF-8) + `Font8` (LATIN-1, `eng/graphics/font8.hpp`) y `Font5x7` (HUD compacto, `eng/graphics/font5x7.hpp`); decodificador mínimo `eng/core/data/utf8.hpp`. Regla: no reimplementar `draw_text` — usar estas utilidades. |

**Demos propias del engine que ya cubren técnicas base** (leer su README para
invariantes y comandos de validación): `000`… `052` (toolchain/copper/blitter/
tiles) y `100`–`107` (scroll/tile-field/dual-playfield/x-limited) y `201`–`202`
(EHB mapa/DPF). El roadmap general (estado y próximas direcciones) está en
[ROADMAP_UNIFICADO.md](../guides/roadmap/ROADMAP_UNIFICADO.md).

---

## 5. Referencia oficial de hardware

| Fuente | Dónde | Uso |
|---|---|---|
| **AHRM 3.ª edición (1990)** | [ahrm/README.md](../reference/ahrm/README.md) + [índice](../reference/ahrm/amiga-hardware-manual-index.md) + texto `.cat.md` | Consultar **registros/bits**, mecanismos (sprite, blitter, copper, audio) y comportamiento ECS/AGA/A3000. |
| **Fuentes autoritativas y reglas** | [amiga-authoritative-sources.md](../reference/amiga-authoritative-sources.md) | Qué fuente es primaria, cuáles didácticas, cuándo contrastar. |
| **Reglas reutilizables A500** | [amiga-a500-dma-copper-state-rules.md](../reference/amiga/hardware/amiga-a500-dma-copper-state-rules.md) | Invariantes DMA/copper/scroll probadas. |
| **Matriz chipsets** | [amiga-chipset-matrix.md](../reference/amiga/hardware/amiga-chipset-matrix.md) | OCS/ECS/AGA: qué registros revisar primero. |
| **Chipset invariantes (índice)** | [amiga-hardware-invariants-microtests.md](../reference/amiga/hardware/amiga-hardware-invariants-microtests.md) | Invariantes MI01–MI10 y su evidencia (demo/test/doc) o pendiente. |

---

## 6. Protocolo de ingesta de repos/referencias externas

Regla general: **conservar e indexar solo la mejor información.** No se mantienen
duplicados; si una fuente dice lo mismo que otra, prevalece la mejor y la otra se
descarta (o se integra su contenido no repetido si aporta algo distinto).

Cuando se aporta un **repositorio, directorio o documento externo** (por ejemplo
`demoscene-repo/effects/…`, un manual OCR, un repo de demos/engine…), seguir este
orden:

1. **Buscar primero lo que ya existe** en este repo:
   - ¿Ya está documentado un efecto/ técnica equivalente? Consulta §4 y el
     coverage-index. ¿Ya existe la referencia (AHRM ediciones, fuentes
     autoritativas)? Consulta §5.
2. **Evaluar la calidad** de lo nuevo frente a lo existente:
   - Comparar profundidad, exactitud y utilidad para el engine (caso real).
   - Criterio: lo nuevo **supera**, **complementa** o **repite** lo ya presente.
3. **Decidir el destino**, según el resultado:
   - **Sustituir**: solo si lo nuevo es claramente superior y lo viejo es obsoleto
     o erróneo. Mover el anterior a `legacy/` o históricos con banner, y anotar la
     decisión.
   - **Componer/fusionar**: si cada fuente aporta algo distinto y no duplica (p. ej.
     un manual completo + una ficha técnica resumen). El contenido repetido siempre
     se descarta; no se mantienen dos documentos que digan lo mismo.
   - **Añadir como referencia**: si es material de consulta que no contradice ni
     duplica → carpeta canónica de `docs/reference/<plataforma>/…`.
   - **Catálogo de efecto**: añadir fila en el coverage-index y abrir ficha en la
     demo si se replica.
   - **Descartar/anotar**: si es inferior o duplicado, dejar una nota de decisión
     en `amiga-authoritative-sources.md` (o README de la carpeta) explicando por
     qué no se incorporó.
4. **No copiar binarios/media**: volcar solo texto/markdown; la media va a `out/`
   (gitignored). Los assets *fuente* van a `assets/<plataforma>/<dominio>/`.
5. Respeta el formato de docs (word wrap, español, diagramas ASCII) y la regla de
   evidencia. La decisión de ingesta (sustitución/descarte) se refleja en qué
   queda en el repo; no hace falta una nota de proceso en el documento de
   referencia.

---

## 7. Enlaces rápidos transversales

- Índice maestro de docs: [docs/README.md](../README.md)
- Roadmap vigente: [ROADMAP_UNIFICADO.md](../guides/roadmap/ROADMAP_UNIFICADO.md)
- Bitácora del scroll por tiles (histórico): [BITACORA_SCROLL_TILES.md](../guides/roadmap/BITACORA_SCROLL_TILES.md)
- Coverage efecto → estado: [demoscene-repo-coverage-index.md](../demos/effects/demoscene-repo-coverage-index.md)
- Fuentes autoritativas: [amiga-authoritative-sources.md](../reference/amiga-authoritative-sources.md)
- Entorno IA (Ollama, sesión, pruebas): [ai-dev-environment/README.md](README.md)

---

## 8. Herramientas y rutas operativas de alto valor

- **Build/run/analyze**: [BUILD_AND_RUN.md](../build/BUILD_AND_RUN.md) (comandos, runner, emulador, herramientas locales).
- **Bucle de entrada del engine**: `engine/include/eng/engine.hpp` (`update -> wait_vblank -> render`; `render` es el punto de commit); backend Amiga: `engine/src/platform/amiga/amiga_minimal.cpp`.
- **Depuración interactiva**: `tools/debug/build-current-demo.sh` (compila con `-O0` el archivo en primer plano a `out/debug-current/`) + F5 con la config «Amiga 500: depurar archivo actual».
- **Breakpoints y memoria en caliente**: `tools/debug/step-memory.mjs`.
- **Self-test del harness** (canal lateral/READY/fps): `node tools/debug/verify-harness.mjs [--strict-fps --warp]`. Nota: el throughput del emulador (~11 fps) limita el gate fps absoluto.
- **Perfiles y visión local**: [tools/profile/README.md](../../tools/profile/README.md). Para la IA: `node tools/profile/ai-analyze.mjs <outName> [frames] --prompt "…"` captura por canal lateral, extrae frames y analiza con Ollama local (sin tokens de nube); `--demo <demo>` lanza WinUAE directo y lo apaga al terminar.
- **Validación temporal por demo**: `demos/amiga/101_ehb_tile_scroll_driver/analyze-sequence.sh`.
- **Scroll multi-modo (XYLimited)**: demos `demos/amiga/201_ehb_map` (8-way EHB) y `202_xlimited_dpf`; test host `node tools/analyze/verify-tile-scroll-modes.mjs`.
- **Checklist del corkscrew XYLimited (201)**: §7 de `demos/amiga/201_ehb_map/src/README.md` (invariantes del anillo vertical, `block_videoposy` y `visible_tile_bias`).
- **Pipeline de tiles/EHB y herramienta todo-en-uno**: `tools/amiga-tiles/README.md` + [PIPELINE_TILES_EHB.md](../demos/tile-pipeline/PIPELINE_TILES_EHB.md) + [REGLAS_PIPELINE_TILES.md](../guides/roadmap/REGLAS_PIPELINE_TILES.md).
- **Harness DAP sin VS Code**: `tools/dap-test/README.md`.
- **Reinstalar el entorno en otro equipo**: [setup-nuevo-equipo.md](../debugging/system/setup-nuevo-equipo.md).
- **Historial de fixes de depuración**: [historial-cambios.md](../debugging/system/historial-cambios.md).