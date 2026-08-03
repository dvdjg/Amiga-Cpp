# Arquitectura del engine

Diseño del **engine C++23 para Amiga 500** de este repositorio. Todo lo relativo al engine
en C del repo hermano (`Cursor-Amiga-C`) está en [../c-engine/](../c-engine/README.md).

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [ROADMAP_ENGINE_CPP_AMIGA500.md](ROADMAP_ENGINE_CPP_AMIGA500.md) | Hoja de ruta completa del engine C++ por fases (0-12) y libreta de diseño. |
| [CODING_STYLE.md](CODING_STYLE.md) | Restricciones y estilo: `gnu++23`, sin exceptions, sin RTTI, sin asignación dinámica en gameplay. |
| [GRAPHICS_DRIVERS.md](GRAPHICS_DRIVERS.md) | Modelo de drivers gráficos (estrategia de composición), `EhbScene` implementado y drivers planificados. |
| [MEMORY_MODEL.md](MEMORY_MODEL.md) | Modelo de memoria del perfil `A500_1MB_Slow`: arenas Chip/Slow/Frame. |
| [HARDWARE_AND_ROM_KERNEL_POLICY.md](HARDWARE_AND_ROM_KERNEL_POLICY.md) | Política close-to-metal: cuándo usar hardware directo y cuándo el ROM kernel. |
| [RETRO_ENGINE_API_BENCHMARK.md](RETRO_ENGINE_API_BENCHMARK.md) | Benchmark de APIs retro (ACE, Scorpion, UAF) para orientar la API objetivo del engine. |

## Puntos de entrada del código

- Bucle del engine: `engine/include/amg/engine.hpp` (`update -> wait_vblank -> render`).
- Backend Amiga: `engine/src/platform/amiga_minimal/amiga_minimal.cpp`.
- Headers del engine: `engine/include/amg/` (core, memory, graphics, scene, platform, debug).

## Histórico

El historial de decisiones y los documentos del anterior engine en C viven en
[../c-engine/](../c-engine/README.md). Si una propuesta de este repo evoluciona, empieza en
esta carpeta; si es una lección del proyecto C, enlázala desde `c-engine/`.
