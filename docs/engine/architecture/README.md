# Arquitectura del engine

Diseño del **engine C++23 para Amiga 500** de este repositorio. Todo lo relativo al engine
en C del repo hermano (`Cursor-Amiga-C`) está en [../c-engine/](../c-engine/README.md).

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [ROADMAP_ENGINE_CPP_AMIGA500.md](ROADMAP_ENGINE_CPP_AMIGA500.md) | Hoja de ruta completa del engine C++ por fases (0-12) y libreta de diseño. |
| [CODING_STYLE.md](CODING_STYLE.md) | Restricciones y estilo: `gnu++23`, sin exceptions, sin RTTI, sin asignación dinámica en gameplay. |
| [MATH_LIBRARY.md](MATH_LIBRARY.md) | Modelo de la librería de matemáticas genérica: `Fixed<Repr,Exp,Policy>`, `Vec`/`Mat`/`Affine`, escalar como parámetro de plantilla. |
| [SCALAR_LIBRARY.md](SCALAR_LIBRARY.md) | **Estructura de la librería escalar-independiente**: capas (algoritmo genérico ↔ rasgos ↔ escalar concreto ↔ backend), `numeric_traits` y guards de límites en compilación, límites por algoritmo. |
| [MINIFLOAT16.md](MINIFLOAT16.md) | Escalar de coma flotante de 16 bits `MiniFloat16` para 68000: formato 1\|5\|10, rango/precisión, rangos seguros de uso y coste. |
| [GRAPHICS_DRIVERS.md](GRAPHICS_DRIVERS.md) | Modelo de drivers gráficos (estrategia de composición), `EhbScene` implementado y drivers planificados. |
| [MEMORY_MODEL.md](MEMORY_MODEL.md) | Modelo de memoria del perfil `A500_1MB_Slow`: arenas Chip/Slow/Frame. |
| [HARDWARE_AND_ROM_KERNEL_POLICY.md](HARDWARE_AND_ROM_KERNEL_POLICY.md) | Política close-to-metal: cuándo usar hardware directo y cuándo el ROM kernel. |
| [RETRO_ENGINE_API_BENCHMARK.md](RETRO_ENGINE_API_BENCHMARK.md) | Benchmark de APIs retro (ACE, Scorpion, UAF) para orientar la API objetivo del engine. |
| [XYLIMITED_ALGORITMO_GENERICO.md](XYLIMITED_ALGORITMO_GENERICO.md) | Algoritmo XYLimited/corkscrew en términos de plataforma (independiente de Amiga), invariantes del anillo y crítica del diseño del API (scroll vs Scene vs HUD). |
| [PLAYFIELD_SCROLL_ARCHITECTURE.md](PLAYFIELD_SCROLL_ARCHITECTURE.md) | **Modelo objetivo** de playfields y scroll: separación algoritmo ↔ superficie ↔ composición ↔ mapping Amiga (`Playfield<N>`, `PlaneView`, estrategias, `DisplayComposition`). Contrato de refactor. |
| [DISPLAY_COMPOSITION.md](DISPLAY_COMPOSITION.md) | **Contrato de buffers y copper**: las tres granularidades (display / un plano / solo copperlist), `MultiBuffered<Driver,N>` como única capa de buffers, y el supervisor `CopperPlan` que falta para orquestar el copper a nivel de escena. |
| [FAST_SCROLL.md](FAST_SCROLL.md) | **Política de scroll rápido** (modo Sonic/Superfrog): `Fill` (`Progressive`/`TileBurst<N>`/`StripPrerender<C>`), `GuardPolicy`, dirección laceda a frontera de tile, tear-free y presupuesto. |
| [PUBLIC_API.md](PUBLIC_API.md) | Principios de la **API pública**: la aplicación no ve hardware; planner que deduce la composición; tipos fuertes, handles, errores sin excepciones, init/frame. |
| [INTERNAL_TYPE_SYSTEM.md](INTERNAL_TYPE_SYSTEM.md) | **Sistema de tipos internos** (tipo Rust): vistas con tag de dominio (`Bytes`/`Words`), unidades fuertes, direcciones/base, handles; auditoría de punteros crudos y frontera `unsafe` en el backend. |
| [SCENE_AND_RESOURCES.md](SCENE_AND_RESOURCES.md) | Escena **retenida** (fuente única de recursos), **modelo de ocupación** (headroom/can_add), introspección de depuración y **representación de actores** (sprite/BOB/playfield) elegida por el engine. |
| [CONTENT_AND_TILEMAP.md](CONTENT_AND_TILEMAP.md) | Contenido de alto nivel: **motor de tiles** (`Tileset`/`TileSource`), **mundo disperso** (chunks + streaming), **sprites/animaciones** y **audio**, con el pipeline de assets. |
| [WORLD_FORMAT.md](WORLD_FORMAT.md) | **Formato de mundo incrustable**: chunk `WorldMap` sobre UAF-R, directorio de chunks ordenado + celdas de índice de banco; mapeo a `SparseTileMap`/`StreamingWorldMap`/`TileMapView` y pipeline `pack-world.mjs`. |
| [STREAMING_LOADER.md](STREAMING_LOADER.md) | **Diseño del Loader** de chunks: preload a RAM, `trackdisk.device` y trackloader de hardware (CIA-B + Paula Disk DMA + decode MFM), doble buffer de pista e integración con `ChunkCache`. |
| [CIRCULAR_VS_XLIMITED.md](CIRCULAR_VS_XLIMITED.md) | Comparativa canónica del modelo circular frente al XLimited interleaved (geometría 352/384, plane-shift, saveword). |
| [ENGINE_DESIGN.md](ENGINE_DESIGN.md) | Diseño integral del engine: capas, entidades y dependencias para un juego completo (gráficos, E/S, sonido, música, escena, assets, ciclo de vida), encaje de demoscene-repo/amiga-bootcamp/ACE/Sevgi y plan de conversión del sistema actual. |
| [VISUAL_EFFECT_SPRITE_DESIGN.md](VISUAL_EFFECT_SPRITE_DESIGN.md) | Parte gráfica/efectos del diseño: `Visual`, `CopperIntent`, `SpriteTemplate` y concept `Effect`; mapa de layers de display, simetría actor/playfield. |
| [AUDIO_MIXER.md](AUDIO_MIXER.md) | Audio Mixer 3.7 de Photon integrado de forma nativa: requisitos de muestras preprocesadas, capacidades, configuración, API, preprocesado y rendimiento. |
| [MUSIC_PLAYER.md](MUSIC_PLAYER.md) | Reproductores de música (ptplayer/P61/AHX): estado de importación, API y convenio mixer+música. |
| [GAME_AUDIO.md](GAME_AUDIO.md) | Capa de audio de juego (`GameAudio`/`SampleBank`): política de voces y ducking, y guía para generar música y sonidos desde herramientas externas. |
| [C2P_BLITTER.md](C2P_BLITTER.md) | C2P (chunky→planar) por Blitter: las 13 fases del C2P 4bpp de `fire-rgb`, minterms (`0xE4`/`0xD8`), máscaras `BLTCDAT`, síncrono vs interrupción de blit y API del engine. |
| [BACKGROUND_TASKS.md](BACKGROUND_TASKS.md) | Tareas de fondo cooperativas (`eng::task::BackgroundQueue`): progreso/rendimiento baratos, adaptación de carga por `vpos`, prioridad al bucle principal y drenado en el hueco de VBlank. |

## Puntos de entrada del código

- Bucle del engine: `engine/include/eng/engine.hpp` (`update -> wait_vblank -> render`).
- Backend Amiga: `engine/src/platform/amiga_minimal/amiga_minimal.cpp`.
- Headers del engine: `engine/include/eng/` (core, memory, graphics, scene, platform, debug).

## Histórico

El historial de decisiones y los documentos del anterior engine en C viven en
[../c-engine/](../c-engine/README.md). Si una propuesta de este repo evoluciona, empieza en
esta carpeta; si es una lección del proyecto C, enlázala desde `c-engine/`.
