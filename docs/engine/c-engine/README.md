# Engine en C (histórico / prior art)

> **Procedencia:** esta carpeta contiene la documentación del **engine en C** del repo
> hermano `Cursor-Amiga-C` (API `engine.h`, batería de pruebas `tests/amiga-battery/`,
> app/menú en `app/`). Se incorpora aquí como **historial y prior art**: las decisiones,
> roadmaps y análisis de ese proyecto alimentaron el engine C++ de este repo, pero ya no
> describen el código activo.

Trata estos documentos como fuente de **conocimiento y contexto**, no como especificación del
código actual. Los enlaces internos pueden referirse a rutas del repo original (`app/`,
`tests/amiga-battery/`, `scripts/`) que no existen aquí.

## Documentos

| Documento | Contenido |
|-----------|-----------|
| [engine-docs-index.md](engine-docs-index.md) | Índice original de la documentación del engine C. |
| [engine-architecture.md](engine-architecture.md) | Visión y arquitectura del engine C (sistema/video/audio/input/memory/debug). |
| [engine-roadmap.md](engine-roadmap.md) | Roadmap maestro del engine C y menú de demos. |
| [engine-implementation-plan.md](engine-implementation-plan.md) | Plan operativo por fases (0-8). |
| [engine-feature-phases.md](engine-feature-phases.md) | Fases de ampliación (núcleo, playfield 2D, BOBs/copper/audio). |
| [engine-subsystems.md](engine-subsystems.md) | APIs opcionales de `engine_extensions.h`. |
| [engine-migration-log.md](engine-migration-log.md) | Registro de la migración del `main.c` monolítico al engine. |
| [engine-new-project-guide.md](engine-new-project-guide.md) | Guía para arrancar un juego/demo con ese engine. |
| [engine-api-maturity-and-refactor-policy.md](engine-api-maturity-and-refactor-policy.md) | Política de madurez y promoción de APIs. |
| [engine-parametric-api-and-cpp-notes.md](engine-parametric-api-and-cpp-notes.md) | APIs paramétricas vs capa retained; notas C vs C++. |
| [engine-cpu-sprites-api-proposal.md](engine-cpu-sprites-api-proposal.md) | Propuesta de API de sprites por CPU. |
| [engine-cpu-sprites-implementation-plan.md](engine-cpu-sprites-implementation-plan.md) | Plan ejecutable de sprites CPU (CS01-CS06). |
| [engine-dynamic-copper-scene-notes.md](engine-dynamic-copper-scene-notes.md) | Copper dinámico tratado como estado de escena. |
| [engine-external-capability-ingestion.md](engine-external-capability-ingestion.md) | Ingesta de capacidades de repos externos (EXTCAP). |
| [external-scroll-source-map.md](external-scroll-source-map.md) | Trazabilidad de fórmulas/patrones de scroll importados. |
| [ace-reuse-notes.md](ace-reuse-notes.md) | Patrones reutilizables del framework ACE. |
| [input-kernel-plan.md](input-kernel-plan.md) | Plan de input Intuition + input.device. |
| [amiga-test-battery-spec.md](amiga-test-battery-spec.md) | Documento maestro de la batería de pruebas del engine C. |
| [engine-test-battery-matrix.md](engine-test-battery-matrix.md) | Matriz subsistema del engine C ↔ caso de batería. |
| [engine-unified-test-roadmap.md](engine-unified-test-roadmap.md) | Plan maestro de pruebas unificadas (taxonomía U/F/T/I). |
| [engine-test-audit-2026-04-06.md](engine-test-audit-2026-04-06.md) | Auditoría de cobertura real de tests. |
| [amiga-implementation-roadmap.md](amiga-implementation-roadmap.md) | Seguimiento maestro PENDIENTE/PARCIAL/HECHO del roadmap C. |

> La validación determinista del engine actual (pixel assertions, secuencias, FrameScope,
> Vision Review) está en [../testing/](../testing/README.md), no en esta carpeta.
