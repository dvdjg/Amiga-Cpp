# Documentación de Amiga-C

Índice maestro del árbol documental del proyecto. Este árbol está organizado por **tema y
rol** (no por origen) para que pueda crecer sin reordenarse: cada documento nuevo debe ir a
la carpeta de su tema.

El proyecto construye un **engine de juegos retro con conectores para distintas plataformas**,
empezando por Amiga 500. El repositorio mantiene dos flujos claramente separados:

- **Engine C++23 (`engine/`, `demos/`, `tools/`)**: flujo activo, con build/run/analyze
  orquestados por PowerShell + Node y validación determinista (canal lateral, pixel
  assertions, secuencias, FrameScope, Vision Review).
- **Proyecto C legado (`legacy/`)**: demo Amiga en C con música ProTracker, con su propio
  `legacy/Makefile` y config de depuración F5 en `legacy/.vscode/`. No mezclar con el engine.

## Cómo empezar

1. **Para continuar el trabajo**: lee [CONTINUATION_CONTEXT.md](CONTINUATION_CONTEXT.md)
   (estado del proyecto y orden de lectura).
2. **Para operar build/run/analyze**: [build/BUILD_AND_RUN.md](build/BUILD_AND_RUN.md).
3. **Para entender el engine**: [architecture/README.md](architecture/README.md).

## Estructura del árbol

| Carpeta | Contenido |
|---------|-----------|
| [architecture/](architecture/README.md) | Diseño del engine C++ actual: estilo, drivers gráficos, memoria, política de hardware/ROM, benchmarks de API y roadmap. |
| [c-engine/](c-engine/README.md) | **Histórico / prior art**: documentación del engine en C del repo hermano `Cursor-Amiga-C` (arquitectura, roadmaps, batería de pruebas). |
| [hardware/](hardware/README.md) | Conocimiento de bajo nivel del Amiga 500: DMA, copper, ABI 68000, loader, invariantes y reglas reutilizables. |
| [techniques/](techniques/README.md) | Fichas de técnicas de programación Amiga (módulos, dual playfield, copper chunky, audio, sprites...). |
| [build/](build/README.md) | Toolchain, build CLI, artefactos y formatos de disco. |
| [emulation/](emulation/README.md) | WinUAE, extensión amiga-debug, MCP, canal lateral, perfiles y automatización del emulador. |
| [debugging/](debugging/README.md) | Sistema de depuración WinUAE-DBG (arquitectura, arreglos) y guías de depuración con IA. |
| [testing/](testing/README.md) | Validación: pixel assertions, secuencias de frames, FrameScope y Vision Review. |
| [demoscene/](demoscene/README.md) | Importación y réplica de efectos demoscene, índices y análisis de efectos concretos. |
| [methodology/](methodology/README.md) | Metodología de desarrollo, runbooks de agentes IA, prompts y automatización. |
| [roadmap/](roadmap/XLIMITED_8WAY_EHB_201.md) | **Roadmaps activos**: scroll 8-way X-Limited + demo 201 EHB (mapa real) y las **reglas de oro del pipeline de tiles/EHB** para no repetir errores. |
| [TILED](roadmap/TILED.md) | **Conocimiento preservado del formato Tiled** (.tmx/.tsx, gids, capas) + nota sobre `png2amiga` como conversor futuro. |
| [reference/](reference/README.md) | Fuentes autoritativas: Manual de Referencia de Hardware de Amiga (AHRM), fuentes objetivas y curso AMC (Wrobel). |
| [legacy/](legacy/README.md) | Notas históricas de troubleshooting con ortografía irregular, en cuarentena. |
| [ai-dev-environment/](ai-dev-environment/README.md) | Mapa del entorno IA: MCP/WinUAE, canal lateral, evidencias y Ollama local. |

## Procedencia del contenido

Parte de esta documentación procede del repo hermano **`Cursor-Amiga-C`** (engine en C con
batería de pruebas y sistema de depuración WinUAE-DBG). Ese material se incorporó **por tema**:

- Conocimiento reutilizable (hardware, técnicas, depuración, WinUAE/MCP, metodología) se
  fusionó con los documentos de este repo en las carpetas correspondientes.
- Los documentos de **arquitectura del engine C** (diseño, roadmaps, migraciones, batería de
  pruebas C) quedaron en [c-engine/](c-engine/README.md) como historial y prior art.
- Las notas de troubleshooting antiguas con ortografía irregular quedaron en
  [legacy/](legacy/README.md).

> Los enlaces internos de los documentos importados pueden apuntar a la estructura del repo
> original (p. ej. `app/`, `tests/amiga-battery/`, `scripts/`). Trátalos como referencia
> histórica; la fuente de verdad operativa es este repositorio.

## Dónde va cada documento nuevo

| Si el documento trata de... | va en... |
|-----------------------------|----------|
| Diseño, API, drivers, memoria o roadmap del engine C++ | `architecture/` |
| Arquitectura o roadmaps del engine en C (histórico) | `c-engine/` |
| Registros, DMA, chipset, ABI, loader del Amiga | `hardware/` |
| Una técnica concreta de composición/efecto | `techniques/` |
| Compilar, toolchain, artefactos, ADF/HDF | `build/` |
| WinUAE, MCP, canal lateral, perfiles, hot-reload | `emulation/` |
| Depuración (gdbserver, breakpoints, DAP/MI/RSP) | `debugging/` |
| Validación automática de frames/píxeles | `testing/` |
| Importación de efectos demoscene | `demoscene/` |
| Procesos, agentes IA, runbooks | `methodology/` |
| Fuentes externas (manuales, cursos) | `reference/` |
| Notas históricas sin limpiar | `legacy/` |
| Operación IA, Ollama, evidencias y mapa de proyectos | `ai-dev-environment/` |
| Roadmaps de desarrollo activos (scroll/tiles/EHB) | `roadmap/` |

> **Documentos nuevos → enlazarlos SIEMPRE aquí (o en ANGELES/AGENTS.md).** Un documento sin
> enlace desde este índice o desde `AGENTS.md` se pierde para futuras sesiones.
