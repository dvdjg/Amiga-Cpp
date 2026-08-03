# Índice de documentación del engine

Punto de entrada a la documentación del motor de juegos.

## Documentos principales

| Documento | Contenido |
|-----------|-----------|
| [engine-cpu-sprites-implementation-plan.md](engine-cpu-sprites-implementation-plan.md) | Plan por fases para llevar sprites CPU y su capa retained desde diseno a bateria e implementacion real. |
| [engine-dynamic-copper-scene-notes.md](engine-dynamic-copper-scene-notes.md) | Guia para tratar copper dinamico como estado de escena: listas fijas, parcheo por frame, doble buffer e invariantes. |
| [engine-api-maturity-and-refactor-policy.md](engine-api-maturity-and-refactor-policy.md) | Politica para promover primero capacidad funcional al engine y refactorizarla despues cuando aparezcan patrones reales y consumidores multiples. |
| [engine-parametric-api-and-cpp-notes.md](engine-parametric-api-and-cpp-notes.md) | Politica de APIs parametricas vs capa retained, familias especializadas por contexto y evaluacion pragmatica C vs C++ para este repo. |
| [engine-cpu-sprites-api-proposal.md](engine-cpu-sprites-api-proposal.md) | Propuesta concreta de API para sprites CPU: primitivas parametricas, capa retained, anchors, persistencia por frame y plan de tests. |
| [engine-external-capability-ingestion.md](engine-external-capability-ingestion.md) | Inventario operativo para destilar capacidades desde repos externos a APIs low-level/high-level retained del engine, con mapeo a bateria y prioridades. |
| [external-scroll-source-map.md](external-scroll-source-map.md) | Trazabilidad tecnica de formulas/patrones de scroll importados desde ACE y `amiga-stuff/scrolling_tricks`, con destino exacto en modulos del engine. |
| [engine_scene_tilebuffer API](/C:/Users/dvdjg/Documents/programa/AI/Cursor-Amiga-C/engine/include/engine_scene_tilebuffer.h) | Estado retained inicial para tilebuffer (invalidacion por camara + iterador de celdas sucias) construido sobre adapters importados de ACE. |
| [ace-reuse-notes.md](ace-reuse-notes.md) | Que patrones y guardrails merece la pena reutilizar desde ACE, y que partes conviene tomar solo como referencia de arquitectura. |
| [demoscene-repo-import-roadmap.md](demoscene-repo-import-roadmap.md) | Roadmap maestro para importar los programas funcionales de `demoscene-repo` a la bateria y al engine, preservando su documentacion de origen y promoviendo solo la parte reusable. |
| [demoscene-repo-coverage-index.md](demoscene-repo-coverage-index.md) | Indice operativo efecto a efecto para seguir el estado de importacion desde `demoscene-repo` hasta caso de bateria local y posible API reusable del engine. |
| [dx39-layers-original-analysis.md](dx39-layers-original-analysis.md) | Despiece tecnico del efecto `layers`: init, dual playfield, scroll, cambios de modulo, gradientes por raster y plan de recreacion por fases en el repo local. |
| [amiga-a500-dma-copper-state-rules.md](amiga-a500-dma-copper-state-rules.md) | Reglas generales de estado A500 para DMA, copper, dual playfield, scroll y ciclo de frame; base reusable para futuros efectos y juegos. |
| [amiga-postmortems-to-rules.md](amiga-postmortems-to-rules.md) | Bugs reales convertidos en reglas reusables y guardrails del proyecto. |
| [amiga-hardware-invariants-microtests.md](amiga-hardware-invariants-microtests.md) | Plan de microtests por invariante hardware para acumular conocimiento low-level sin depender solo de demos compuestas. |
| [engine-architecture.md](engine-architecture.md) | Visión, alcance (3D blitter, multi-plataforma), estructura del proyecto, conceptos (utils/managers, view/viewport, estados, debug/release), API actual, roadmap de capacidades. |
| [engine-roadmap.md](engine-roadmap.md) | **Roadmap de fases** con tests en emulador: estructura app/ vs engine, menú de demos, efectos, automatización Coppenheimer/Playwright, preparación para juego. |
| [engine-implementation-plan.md](engine-implementation-plan.md) | **Plan de implementación ejecutable**: pasos por fase, tests reproducibles (verify-build.sh), criterios de éxito. Fase 1 implementada. |
| [engine-feature-phases.md](engine-feature-phases.md) | Fases de ampliación del engine (`engine_suite.h`, playfield, lotes BOB, copper dinámico, audio modular). |
| [engine-subsystems.md](engine-subsystems.md) | Subsistemas opcionales: copper avanzado, sprites, joystick, view/tilemap, custom peek/poke, fuente CPU, DOS, política de audio. |
| [engine-test-battery-matrix.md](engine-test-battery-matrix.md) | Matriz subsistema ↔ caso de batería + evidencia; backlog (plan) para cobertura del engine. |
| [engine-unified-test-roadmap.md](engine-unified-test-roadmap.md) | **Plan maestro de tests del engine**: niveles U/F/T/I, trazas `BTEV`, huella visual para visión, fases por subsistema, técnicas de juego, automatización. |
| [engine-test-audit-2026-04-06.md](engine-test-audit-2026-04-06.md) | Auditoría inicial de cobertura real del engine: huecos, desalineaciones entre docs y primer lote recomendado. |
| [roadmap-amc-wrobel-engine-docs-and-debug.md](roadmap-amc-wrobel-engine-docs-and-debug.md) | Curso AMC (Mark Wrobel): ingerir espejo local/web, mapa ↔ engine, docs aditivas, auditoría de API, WinUAE/MCP y tests automáticos. |
| [reference/amc-wrobel/README.md](reference/amc-wrobel/README.md) | Carpeta destino del índice de lecciones, topic map y análisis de gaps (rellenar según el roadmap). |
| [engine-new-project-guide.md](engine-new-project-guide.md) | **Nuevo proyecto**: arrancar juego/demo (dentro del repo o repo aparte), patrones create/loop/destroy, includes, build, checklist. |
| [verificación-display-por-ia.md](verificación-display-por-ia.md) | **Verificación del display por la IA**: amigaprofile (yo leo PNG), LM Studio (script con visión), Coppenheimer + Playwright. Script `verify-display-with-lmstudio.mjs`. |
| [demoscene-effects-integration.md](demoscene-effects-integration.md) | Integración de efectos demoscene: catálogo de técnicas, mapeo a APIs del engine, gui.c como referencia, planes de UI, uso del overlay WinUAE. |
| [development-methodology.md](development-methodology.md) | Metodología general: fases, pruebas, depuración; sección 7.1 aplicada a C/Amiga. |
| [debug-with-ai.md](debug-with-ai.md) | Depuración con IA: MCP, dap-proxy, mcp-amiga-debug, compilación para depuración (-Og/-O0). |
| [amiga-lowlevel-agent-prompt.md](amiga-lowlevel-agent-prompt.md) | Prompt maestro para pedir a la IA trabajo close-to-the-metal sin implementaciones big-bang ni iteraciones a ciegas. |
| [amiga-lowlevel-technique-contract-template.md](amiga-lowlevel-technique-contract-template.md) | Plantilla de contrato tecnico para separar init, VBL, copper/DMA, invariantes y evidencia antes de implementar. |
| [amiga-display-setup-checklist.md](amiga-display-setup-checklist.md) | Checklist para heredar correctamente contratos de display ya validados y evitar reabrir fallos como `BPL1MOD/BPL2MOD` mal configurados. |
| [mcp-live-coding-workflow.md](mcp-live-coding-workflow.md) | Flujo winuae-emu: compilar, desplegar, verificar; `winuae_exec_chunk`; modos live; multi-máquina. |
| [amiga-dev-harness-loader.md](amiga-dev-harness-loader.md) | Estrategia de cargador/harness de desarrollo: ADF/OS-loader, carga directa diagnóstica y futuro dev disk o kernel ligero para automatización. |
| [amiga-kernel-loader-notes.md](amiga-kernel-loader-notes.md) | Modelo de Exec + AmigaDOS para carga de binarios, documentado primero para **A500 + Kickstart 1.3** y marcando aparte lo que es posterior o más propio de A600/A1200. |
| [m68k-stack-and-calling-notes.md](m68k-stack-and-calling-notes.md) | ABI GCC/m68k: pila, `A6`, registros scratch, retorno en `D0`, `-mrtd`, `-mshort` y exception frames. |
| [amiga-binary-and-disk-formats.md](amiga-binary-and-disk-formats.md) | Qué significan `ADF`, `.map`, `.exe`, `.elf`, `.o`, `.out`, HDF/RDB y cómo encajan en el flujo del repo. |
| [ia-hot-reload-winuae.md](ia-hot-reload-winuae.md) | Documento maestro del objetivo de ejecución en caliente con IA: qué se quiere conseguir, qué ya funciona, qué falló, qué falta y de qué fuentes sale el conocimiento usado. |
| [techniques/README.md](techniques/README.md) | Fichas técnicas (modulo, dual layer, copper chunky, audio, sprites, DPF bobs) + lab menú. |
| [amiga-chipset-matrix.md](amiga-chipset-matrix.md) | Matriz OCS/ECS/AGA/CD32 y punteros al AHRM. |
| [amiga-test-battery-spec.md](amiga-test-battery-spec.md) | **Batería de tests** + **visión IA** (ADF caliente, snapshot máquina, decode bitmaps, depuración paso a paso) y **hoja de ruta MCP** §10. |
| [amiga-implementation-roadmap.md](amiga-implementation-roadmap.md) | **Seguimiento de implementación:** estados PENDIENTE/PARCIAL/HECHO, fases A–E (MCP, infra tests, IDs §8, engine, docs). |
| [../tests/amiga-battery/README.md](../tests/amiga-battery/README.md) | Índice práctico de la batería: plantilla, harness común, casos abiertos y convenciones de evidencia. |
| [../tests/amiga-battery/common/README.md](../tests/amiga-battery/common/README.md) | Harness común de la batería y criterio de cuándo sus utilidades deben migrar al engine. |
| [diagnóstico-depurador-f5.md](diagnóstico-depurador-f5.md) | Diagnóstico cuando F5 o los breakpoints no funcionan. |
| [winuae-extensión-internals.md](winuae-extensión-internals.md) | Cómo la extensión amiga-debug accede a WinUAE (GDB, memoria, Custom). |

## Fuentes objetivas de hardware/kernel

- [reference/amiga-authoritative-sources.md](reference/amiga-authoritative-sources.md) - inventario de fuentes técnicas validas para respaldar implementaciones y tests (base A500/Kickstart 1.3).

## Otras referencias

- [amigaprofile-format.md](amigaprofile-format.md) – Formato de perfiles de ejecución.
- [agent-runbook.md](agent-runbook.md) – Uso del agente IA en el proyecto.
- [amiga-lowlevel-agent-prompt.md](amiga-lowlevel-agent-prompt.md) – Prompt maestro para tareas low-level Amiga.
- [amiga-lowlevel-technique-contract-template.md](amiga-lowlevel-technique-contract-template.md) – Contrato tecnico por fases, invariantes y evidencia.
- [agent-system-roadmap.md](agent-system-roadmap.md) – Sistema de agentes (G0–G5) para ejecutar y supervisar el roadmap Amiga.
- [amiga-hardware-manual-index.md](amiga-hardware-manual-index.md) – **Índice del Amiga Hardware Reference Manual (3rd ed.)**: capítulos, apéndices, registros y términos de búsqueda para consultar el manual completo (`Amiga Hardware Reference Manual 3rd edition (Commodore Amiga Inc.) (Z-Library).cat.md`).
- [config/winuae/README.md](../config/winuae/README.md) – Perfiles WinUAE A500/A1200/CD32 para MCP y extensión.

## Objetivo del engine y papel de la batería

- El objetivo del proyecto es construir un **engine reusable de juegos Amiga**, no una colección inconexa de demos o pruebas.
- La batería `tests/amiga-battery/` debe servir para demostrar que una técnica de hardware concreta funciona y para capturar su procedimiento de validación.
- Cuando una técnica quede suficientemente entendida, la dirección correcta es mover su lógica reusable hacia `engine/` y hacer que el test pase a usar esa API en lugar de duplicar inicialización de hardware.
- La app en `app/` es el integrador principal del engine; la batería es el laboratorio controlado para ampliar el engine con seguridad.

## Ruta recomendada de lectura

1. [engine-new-project-guide.md](engine-new-project-guide.md) — si vas a crear un juego/demo encima del motor.
2. [engine-architecture.md](engine-architecture.md)
3. [engine-roadmap.md](engine-roadmap.md)
4. [amiga-test-battery-spec.md](amiga-test-battery-spec.md)
5. [amiga-implementation-roadmap.md](amiga-implementation-roadmap.md)
6. [../tests/amiga-battery/README.md](../tests/amiga-battery/README.md)
7. [amiga-kernel-loader-notes.md](amiga-kernel-loader-notes.md)
8. [m68k-stack-and-calling-notes.md](m68k-stack-and-calling-notes.md)
9. [amiga-binary-and-disk-formats.md](amiga-binary-and-disk-formats.md)

## Tipos de documentación previstos

- **Instalación / build**: compilador, dependencias, Makefile, tareas del IDE (ya cubierto en README y `.vscode`).
- **Nuevo proyecto con el engine**: [engine-new-project-guide.md](engine-new-project-guide.md) (estructura, patrones del repo, build, checklist; repo nuevo o efecto dentro de `app/`).
- **Tutorial largo**: pasos guiados (hola mundo → view → blitter → BOBs → máscaras → fuentes → paletas → audio → tilemaps → copper → sprites). Pendiente como doc narrativo aparte; la guía de nuevo proyecto cubre el esqueleto operativo.
- **Referencia API**: `engine.h`, tipos; Doxygen o manual.
- **Herramientas**: conversión de paletas, bitmaps, tiles, fuentes, audio; ver `gfx/` y `doc/tools/` si se crea.
- **Contribución**: estilo de código, cómo proponer cambios.

## Referencia externa

- Para comparar patrones de API (viewport, tilemap, estados) conviene revisar motores open source recientes para Amiga en C cuando una función concreta del roadmap lo requiera.
