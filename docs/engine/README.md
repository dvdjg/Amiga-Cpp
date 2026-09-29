# Documentación del engine

Hub **navegable** de la documentación del engine C++23. Organiza los documentos por **área**
(no por origen) para crecer sin reordenar. La organización canónica de `docs/` está en
[STRUCTURE.md](../STRUCTURE.md) §10; el índice maestro, en [docs/README.md](../README.md); el
mapa «tarea → documentación», en [DOC-MAP-PRINCIPAL.md](../ai-dev-environment/DOC-MAP-PRINCIPAL.md).

> Los documentos viven en `docs/engine/architecture/` (salvo los roadmap en
> `docs/guides/roadmap/`). Este hub solo **enlaza** a los canónicos: no duplica su contenido.

## Áreas

### API de aplicaciones (lo que ve un juego/app)

- [PUBLIC_API.md](architecture/PUBLIC_API.md) — principios de la API pública (la app no ve hardware; intuitivo/simple/sin restringir).
- [PUBLIC_GAME_API.md](architecture/PUBLIC_GAME_API.md) — **fachada de juego** (`eng::App`/`Screen`/`World`/`Device`) y mapeo desde el API interno.
- [GAME_API_TWO_LEVELS.md](architecture/GAME_API_TWO_LEVELS.md) — **intención del API de juego**: dos niveles (dominio por defecto + escape *close-to-the-metal*), reglas y checklist.
- [ROADMAP_API_COHERENCE.md](architecture/ROADMAP_API_COHERENCE.md) — diagnóstico + **plan por fases** (F1–F7) hacia una API coherente; incluye **consumidores externos** (§7) y la ficha del **emulador NES** ([NES_CONSUMER.md](NES_CONSUMER.md)).
- [ROADMAP_ENGINE_CPP_AMIGA500.md](architecture/ROADMAP_ENGINE_CPP_AMIGA500.md) — hoja de ruta completa del engine por fases.

### Sistema interno de abstracciones (capas)

- [ENGINE_2D_ABSTRACCIONES.md](architecture/ENGINE_2D_ABSTRACCIONES.md) — capas de abstracción y APIs (punto de entrada de lectura).
- [ENGINE_DESIGN.md](architecture/ENGINE_DESIGN.md) — bucle del engine y modelo de tres planos.
- [ENGINE_STRUCTURE_REVIEW.md](architecture/ENGINE_STRUCTURE_REVIEW.md) — decisiones de consolidación y frontera `eng/api`.
- [PLATFORM_LAYERS.md](architecture/PLATFORM_LAYERS.md) — capas plataforma/backend.
- [HEADER_POLICY.md](architecture/HEADER_POLICY.md) — política de cabeceras.

### Tipos de datos

- [INTERNAL_TYPE_SYSTEM.md](architecture/INTERNAL_TYPE_SYSTEM.md) — tipos de dominio internos (vistas con `Tag`, unidades fuertes, frontera `raw()`, `Block<Tag>`, `MemoryKind`).
- [MEMORY_MODEL.md](architecture/MEMORY_MODEL.md) — modelo de memoria (`MemorySystem`/arenas Chip/Slow/Frame, presupuesto).

### Matemáticas y geometría

- [MATH_LIBRARY.md](architecture/MATH_LIBRARY.md) — `eng::math`: escalares, linalg (Vec/Mat), geometría, ruido.
- [SCALAR_LIBRARY.md](architecture/SCALAR_LIBRARY.md) — biblioteca escalar (tabla función × escalar).
- [MINIFLOAT16.md](architecture/MINIFLOAT16.md) — `MiniFloat16` (formato numérico).
- [EXPRESSION_TEMPLATES.md](architecture/EXPRESSION_TEMPLATES.md) / [TEMPLATE_LIBRARY.md](architecture/TEMPLATE_LIBRARY.md) — plantillas y expresiones.
- [3D_RENDER_VS_PHYSICS.md](architecture/3D_RENDER_VS_PHYSICS.md) / [3D_PHYSICS.md](architecture/3D_PHYSICS.md) — 3D (modelo/render) y física.

### Gráficos, composición y objetos

- [SCENE_COMPOSITION.md](architecture/SCENE_COMPOSITION.md) / [DISPLAY_COMPOSITION.md](architecture/DISPLAY_COMPOSITION.md) — escena y composición de display.
- [GRAPHICS_DRIVERS.md](architecture/GRAPHICS_DRIVERS.md) / [RASTER.md](architecture/RASTER.md) / [C2P_BLITTER.md](architecture/C2P_BLITTER.md) — drivers, rasterizado, chunky→planar.
- [OBJECT_SYSTEM.md](architecture/OBJECT_SYSTEM.md) — **sistema de objetos** (representación, transparencia, fondos, copper por objeto; §15 = repaso de abstracciones/fronteras).
- [VISUAL_EFFECT_SPRITE_DESIGN.md](architecture/VISUAL_EFFECT_SPRITE_DESIGN.md) — `Visual`/`CopperIntent`/`HwSprite*`/`Effect`.
- [SCENE_AND_RESOURCES.md](architecture/SCENE_AND_RESOURCES.md) — escena retenida y ocupación de recursos.
- **Scroll/tiles**: [PLAYFIELD_SCROLL_ARCHITECTURE.md](architecture/PLAYFIELD_SCROLL_ARCHITECTURE.md), [XYLIMITED_ALGORITMO_GENERICO.md](architecture/XYLIMITED_ALGORITMO_GENERICO.md), [FAST_SCROLL.md](architecture/FAST_SCROLL.md), [CONTENT_AND_TILEMAP.md](architecture/CONTENT_AND_TILEMAP.md), [TILE_FIELD_API.md](architecture/TILE_FIELD_API.md), [AMIGA_8WAY_SCROLLING.md](architecture/AMIGA_8WAY_SCROLLING.md).

### GUI

- [GUI_LIBRARY.md](architecture/GUI_LIBRARY.md) — diseño de `eng::ui` (widgets, compositor, backing, cursor hardware).
- [RASTER.md](architecture/RASTER.md) — primitivas CPU/Blitter que usa la GUI.

### Audio

- [GAME_AUDIO.md](architecture/GAME_AUDIO.md) · [AUDIO_MIXER.md](architecture/AUDIO_MIXER.md) · [MUSIC_PLAYER.md](architecture/MUSIC_PLAYER.md) · [AUDIO_STREAMING.md](architecture/AUDIO_STREAMING.md).

### Recursos y carga

- [RESOURCE_SYSTEM.md](architecture/RESOURCE_SYSTEM.md) · [STREAMING_LOADER.md](architecture/STREAMING_LOADER.md) · [WORLD_FORMAT.md](architecture/WORLD_FORMAT.md).

### Mini-SO (`eng::os`)

- [MINI_OS_MESSAGE_LOOP.md](architecture/MINI_OS_MESSAGE_LOOP.md) · [MINI_OS_INPUT.md](architecture/MINI_OS_INPUT.md) · [MINI_OS_TIME.md](architecture/MINI_OS_TIME.md) · [MINI_OS_IO.md](architecture/MINI_OS_IO.md) · [MINI_OS_TASKS.md](architecture/MINI_OS_TASKS.md).

### IA y simulación

- [GAME_AI_LIBRARY.md](architecture/GAME_AI_LIBRARY.md) · [SIM_ECOSYSTEM.md](architecture/SIM_ECOSYSTEM.md) · [NPC_PSYCHOLOGY.md](architecture/NPC_PSYCHOLOGY.md) · [GOAP_EXTENDED.md](architecture/GOAP_EXTENDED.md) · [BOARD_GAME_AI.md](architecture/BOARD_GAME_AI.md) · [CARD_GAME_AI.md](architecture/CARD_GAME_AI.md).

### Estilo, hardware y concurrencia

- [CODING_STYLE.md](architecture/CODING_STYLE.md) — reglas duras de estilo.
- [HARDWARE_INVENTORY.md](architecture/HARDWARE_INVENTORY.md) · [HARDWARE_AND_ROM_KERNEL_POLICY.md](architecture/HARDWARE_AND_ROM_KERNEL_POLICY.md) · [PARALLEL_AND_THREADS.md](architecture/PARALLEL_AND_THREADS.md).

### Consumers / integradores

- [NES_CONSUMER.md](NES_CONSUMER.md) — **emulador NES → Amiga 500**: cómo implementar sus interfaces `I*` sobre el engine, opciones de **scroll** (XYUnlimited vs XYLimited) y **índice de la implementación de referencia**.

## Catálogo completo

- [architecture/README.md](architecture/README.md) — tabla con **todos** los documentos de arquitectura y su propósito.

## Puntos de entrada del código

- Bucle del engine: `engine/include/eng/engine.hpp`.
- Fachada de juego: `engine/include/eng/api/api.hpp` (`App`/`Screen`/`World`/`Device`).
- Backend Amiga: `engine/src/platform/amiga/amiga.cpp`.

## Roadmaps

- [ROADMAP_ENGINE_CPP_AMIGA500.md](architecture/ROADMAP_ENGINE_CPP_AMIGA500.md) (fases del engine).
- [ROADMAP_API_COHERENCE.md](architecture/ROADMAP_API_COHERENCE.md) (coherencia de la API, F1–F7).
- Roadmaps por dominio en [docs/guides/roadmap/](../guides/roadmap/): `ROADMAP_UNIFICADO.md`, `ROADMAP_MINI_OS.md`, `ROADMAP_RESOURCES.md`, `ROADMAP_AUDIO.md`, `ROADMAP_GUI.md`, `ROADMAP_BOARD_GAMES.md`, `ROADMAP_BLITTER_COPPER.md`.
