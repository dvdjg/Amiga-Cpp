# Roadmaps y planes (estado)

Índice de esta carpeta con el **estado de cada documento** (activo / pausado / cerrado / histórico). El estado vigente del proyecto se decide por [ROADMAP_UNIFICADO.md](ROADMAP_UNIFICADO.md); este README solo enruta y marca qué está vivo. Regla (AGENTS.md §1.3): un roadmap o plan nuevo se añade aquí en la misma pasada y se enlaza desde `docs/README.md`.

Leyenda: **ACTIVO** = guía el trabajo actual (aunque tenga fases entregadas y pendientes) · **PAUSADO** = retomable por decisión explícita, no ahora · **CERRADO** = completado o resuelto, se conserva por evidencia · **HISTÓRICO** = superado, solo valor documental · **REGLAS** = contrato vigente, no un plan.

| Documento | Estado | Qué cubre |
|---|---|---|
| [ROADMAP_UNIFICADO.md](ROADMAP_UNIFICADO.md) | **ACTIVO** | Estado real del engine y las demos y próximas direcciones; inventario de roadmaps. Fuente única del rumbo. |
| [ROADMAP_DEUDA_TECNICA.md](ROADMAP_DEUDA_TECNICA.md) | **ACTIVO** | Registro transversal de deuda (DT-NNN) con síntoma, evidencia, impacto y plan; incluye demos NO VERIFICADAS. |
| [DEMOS_A_ELIMINAR_O_MEJORAR.md](DEMOS_A_ELIMINAR_O_MEJORAR.md) | **ACTIVO** | Auditoría viva de demos que no pasan el umbral de calidad, con criterios y regeneración. |
| [ROADMAP_GAME_API.md](ROADMAP_GAME_API.md) | **ACTIVO** | Fachada de juego limpio: pila de escenas, manifiesto de assets, estados; fases abiertas. |
| [ROADMAP_API_COHERENCE.md](ROADMAP_API_COHERENCE.md) | **ACTIVO** | Diagnóstico contrastado del API real y plan por fases (F0–F7) hacia la fachada final; consumidores externos (§7). |
| [ROADMAP_MINI_OS.md](ROADMAP_MINI_OS.md) | **ACTIVO** | Mini-SO de mensajes (`eng::os`) y UI reactiva: entregados M0–M6; pendientes M9–M11. |
| [ROADMAP_GUI.md](ROADMAP_GUI.md) | **ACTIVO** | Librería GUI (`eng::ui`): widgets, compositor con backing store, keymaps; pendiente acelerar el copy con Blitter (G8). |
| [ROADMAP_RESOURCES.md](ROADMAP_RESOURCES.md) | **ACTIVO** | Caché de assets (`eng::res`), E/S asíncrona y DynLoader `.englib`. |
| [ROADMAP_AUDIO.md](ROADMAP_AUDIO.md) | **ACTIVO** | Capa de audio (`eng::audio`): modos/canales, reproductores, integración con el mini-SO. |
| [ROADMAP_AUDIO_COMPRESSOR.md](ROADMAP_AUDIO_COMPRESSOR.md) | **ACTIVO** | Pipeline AUZX/ACP1 de compresión de audio (fases C0–C18). |
| [ROADMAP_AUDIO_COMPRESSOR_REFACTOR.md](ROADMAP_AUDIO_COMPRESSOR_REFACTOR.md) | **ACTIVO** | Estructura host del compresor de audio (batch portable, SDL3). |
| [AUDIO_CORPUS_FREEPD.md](AUDIO_CORPUS_FREEPD.md) | **ACTIVO** | Corpus de pruebas (FreePD) para ajustar los parámetros de los codecs. |
| [ROADMAP_BOARD_GAMES.md](ROADMAP_BOARD_GAMES.md) | **ACTIVO** | Motores de tablero (`eng::board`): ajedrez con variantes y Go 9×9; faltan trackloader Amiga y MCTS ligero. |
| [ROADMAP_CARD_GAMES.md](ROADMAP_CARD_GAMES.md) | **ACTIVO** | Motores de naipes (`eng::cards`): Hold'em con footprint N20–N512; falta el juego con UI (C5). |
| [ROADMAP_NPC_PSYCHOLOGY.md](ROADMAP_NPC_PSYCHOLOGY.md) | **ACTIVO** | Perfil psicológico de NPC sobre `eng::sim` (arquetipos, tells, evolución); fases P0–P5 planificadas. |
| [ROADMAP_GAME_AI.md](ROADMAP_GAME_AI.md) | **ACTIVO** | IA de juego (`eng::ai`): GOAP, decisión, navegación, steering y percepción; fases G1–G7. |
| [ROADMAP_PROCEDURAL_WORLDS.md](ROADMAP_PROCEDURAL_WORLDS.md) | **ACTIVO** | Generación procedural de mundos metroidvania (W0–W12): biomas, grafos, gating, geometría. |
| [ROADMAP_TILED_WORLD_SUPPORT.md](ROADMAP_TILED_WORLD_SUPPORT.md) | **ACTIVO** | Soporte completo de Tiled: parser/cooker, tilesets, capas y streaming de mundos grandes. |
| [ROADMAP_UTIL_LIBRARY.md](ROADMAP_UTIL_LIBRARY.md) | **ACTIVO** | Crecimiento de la librería de utilidades (`eng::util`): stats, color, colisión, texto, grid… |
| [ROADMAP_TIPOS_Y_CASTS.md](ROADMAP_TIPOS_Y_CASTS.md) | **ACTIVO** | Corrección de tipos y retirada de casts forzados, con fases y medición. |
| [ROADMAP_JUEGO_SPRITES_BOBS.md](ROADMAP_JUEGO_SPRITES_BOBS.md) | **ACTIVO** | Adecuación de sprites HW/BOBs para juego: compuestos, trayectorias, mecanismos (F1–F8). |
| [ROADMAP_SPRITES_COMPLETO.md](ROADMAP_SPRITES_COMPLETO.md) | **ACTIVO** | Plan de detalle de las fases **abiertas** de sprites (F3–F7) y tareas de cierre. |
| [ROADMAP_218_CAMPO_UNICO.md](ROADMAP_218_CAMPO_UNICO.md) | **CERRADO** | Campaña de rendimiento de la 218 «SPR Layer»: cerrada en F8 con **50 fps** (recorte autorizado a 3 BOBs); residual de emisión y palancas fieles documentados. |
| [ROADMAP_HW_ZOOM.md](ROADMAP_HW_ZOOM.md) | **ACTIVO** | Zoom hardware vertical (`BPLxMOD`) y horizontal (`BPLCON1`/$102). |
| [ROADMAP_RASTER_ROAD.md](ROADMAP_RASTER_ROAD.md) | **ACTIVO** | Carretera/suelo por raster (`BPLxPT` por línea): perspectiva, curvas y rasantes. |
| [ROADMAP_WORKBENCH.md](ROADMAP_WORKBENCH.md) | **ACTIVO** | Portabilidad a Workbench: UI + mini-SO + servicios del sistema. |
| [REFACTOR_PLAYFIELD_SCROLL.md](REFACTOR_PLAYFIELD_SCROLL.md) | **ACTIVO** | Fases para llevar playfields/scroll al modelo objetivo (algoritmo/superficie/composición/máquina). |
| [REFACTOR_SCALAR_GENERICO.md](REFACTOR_SCALAR_GENERICO.md) | **ACTIVO** | Escalar genérico (Fixed de 32 bits) y portabilidad a 32/64 bits sin duplicar algoritmos. |
| [PLAN_ORGANIZACION_ENGINE.md](PLAN_ORGANIZACION_ENGINE.md) | **ACTIVO** | Organización del engine: estructura, cabeceras, plataformas, tests y scripts. |
| [PLAN_ORGANIZACION_DEMOS.md](PLAN_ORGANIZACION_DEMOS.md) | **ACTIVO** | Organización de `demos/`: techniques/features, variantes y numeración. |
| [NORMALIZACION_REPO.md](NORMALIZACION_REPO.md) | **ACTIVO** | Limpieza tras la mezcla de ramas: una implementación de buffers/copper, superficies sin memoria, cámaras. |
| [SCROLL_DEMOS_CLEANUP.md](SCROLL_DEMOS_CLEANUP.md) | **ACTIVO** | Fase 0 de la limpieza de demos de scroll: inventario, glosario y matriz por algoritmo. |
| [BOARD_LEVEL_AND_STATS.md](BOARD_LEVEL_AND_STATS.md) | **ACTIVO** | Nivel de juego por footprint, medición fiable del cómputo y estadística para los motores de tablero. |
| [REGLAS_PIPELINE_TILES.md](REGLAS_PIPELINE_TILES.md) | **REGLAS** | Reglas de oro del pipeline de tiles/EHB (cuantizar antes de extraer, no romper el 100 %). |
| [ROADMAP_MEMORY_OWNERSHIP.md](ROADMAP_MEMORY_OWNERSHIP.md) | **PAUSADO** | Correcciones de ownership de memoria respaldadas por tests; se retoma con un consumidor y fallo concretos. |
| [ROADMAP_BLITTER_COPPER.md](ROADMAP_BLITTER_COPPER.md) | **PAUSADO** | Blitter ↔ Copper: batch `PaletteLine` hecho; falta perfil A/B en target al reactivar. |
| [ROADMAP_GENERICIDAD_PLANTILLAS.md](ROADMAP_GENERICIDAD_PLANTILLAS.md) | **CERRADO** | Genericidad de plantillas (sin tipos de dato concretos): todo hecho o descartado con motivo. |
| [CONSULTA-SPLIT-208.md](CONSULTA-SPLIT-208.md) | **CERRADO** | Resuelto: el split vertical del corkscrew sobre la línea 255 es un límite de hardware (8 bits de VPOS). |
| [PROBLEMA_LAUNCHER_DEMOS_NUEVAS.md](PROBLEMA_LAUNCHER_DEMOS_NUEVAS.md) | **CERRADO** | Resuelto: demos nuevas que quedaban en AmigaDOS. |
| [ROADMAP_ENGINE_CPP_AMIGA500.md](ROADMAP_ENGINE_CPP_AMIGA500.md) | **HISTÓRICO** | Hoja de ruta inicial del engine (fases 0–12) y libreta de diseño (§20). |
| [XLIMITED_8WAY_EHB_201.md](XLIMITED_8WAY_EHB_201.md) | **HISTÓRICO** | Roadmap original del 8-way EHB (201→202), completado; superado por el roadmap vigente. |
| [BITACORA_SCROLL_TILES.md](BITACORA_SCROLL_TILES.md) | **HISTÓRICO** | Bitácora del scroll por tiles (estado, correcciones y lecciones de 2026-08). |
| [TILED.md](TILED.md) | **HISTÓRICO** | Conocimiento preservado del formato Tiled (.tmx/.tsx) previo al soporte nativo. |
| [PORT_PROMPT_1A1.md](PORT_PROMPT_1A1.md) | **HISTÓRICO** | Método de porte 1:1 desde `demoscene-repo-orig` (cita nombres antiguos de la API). |
