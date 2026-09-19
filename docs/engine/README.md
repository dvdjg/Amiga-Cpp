# Documentación del engine

Todo lo relacionado con el diseño, la arquitectura y la implementación del
engine. Organización canónica en `docs/STRUCTURE.md` §10.

```
docs/engine/
├── architecture/       → capas del engine, memoria, rendering, abstracciones, estilo
│   ├── ENGINE_2D_ABSTRACCIONES.md   → capas de abstracción y APIs del engine
│   ├── RETRO_ENGINE_API_BENCHMARK.md → comparaciones de APIs de engines retro
│   ├── CODING_STYLE.md              → guía de estilo de código (reglas duras)
│   ├── MEMORY_MODEL.md              → modelo de memoria del engine
│   ├── GRAPHICS_DRIVERS.md          → drivers de gráficos
│   ├── GAME_AI_LIBRARY.md           → IA de juego (planificación/GOAP, decisión, navegación…)
│   ├── SIM_ECOSYSTEM.md             → ecosistema vivo (eng::sim): criaturas, mente, sociedad y LOD
│   ├── NPC_PSYCHOLOGY.md            → perfil psicológico de NPC (arquetipos, expresión no verbal, lectura de tells, evolución, convenciones)
│   ├── BOARD_GAME_AI.md             → motores de tablero (ajedrez/Go), footprint y NLG
│   ├── CARD_GAME_AI.md              → motores de naipes (póker): baraja, reglas, equity, IA y simulación
│   ├── PARALLEL_AND_THREADS.md      → concurrencia abstracta (eng::parallel): no-op m68k, std en host
│   ├── 3D_RENDER_VS_PHYSICS.md      → soporte 3D (modelo/render) y por qué no hay física 3D
│   └── …                            → scrolling, DPF, X-Limited, roadmap antiguo
└── c-engine/           → documentación del engine C legado (contexto histórico)
```

## Lectura recomendada
- Empezar por `docs/engine/architecture/ENGINE_2D_ABSTRACCIONES.md` y el
  código de `engine/` (bucle de entrada en `engine/include/eng/engine.hpp`).
- Estilo obligatorio: `docs/engine/architecture/CODING_STYLE.md`.
- Reglas de rendimiento del chipset: `docs/guides/optimization/OPTIMIZACION_GPP_68000.md`.

Los tutoriales y guías de proceso viven en `docs/guides/`; los roadmaps del
engine, en `docs/guides/roadmap/`.