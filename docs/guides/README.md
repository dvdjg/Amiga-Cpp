# Guías del proyecto

Instrucciones y guías de proceso del proyecto: roadmaps vigentes, optimización,
metodología de trabajo y plantillas. Organización canónica en
`docs/STRUCTURE.md` §10.

```
docs/guides/
├── roadmap/             → ROADMAP_VIGENTE (ROADMAP_UNIFICADO.md), planes y decisiones
│   ├── README.md                     → estado de cada roadmap (activo/pausado/cerrado/histórico)
│   ├── ROADMAP_UNIFICADO.md          → estado del engine/demos y próximas direcciones
│   ├── ROADMAP_BOARD_GAMES.md        → motores de tablero (ajedrez/Go) y conocimiento en disquete
│   ├── ROADMAP_CARD_GAMES.md         → motores de naipes (póker): reglas, equity, IA y simulación
│   ├── ROADMAP_NPC_PSYCHOLOGY.md     → perfil psicológico de NPC: arquetipos, expresión, lectura de tells y evolución
│   ├── ROADMAP_RPG.md                → capa RPG/CRPG: núcleo de mundo, motores de progreso/conflicto/sociedad/narrativa y vertical slice
│   ├── ROADMAP_ADVENTURE.md          → capa de aventura gráfica (eng::adv): walkboxes, cámara, verbos, diálogo, Stage Composer y vertical slice
│   ├── ROADMAP_ADVENTURE_IMPORT.md   → pipeline host de importación: SCUMM/AGI/SCI → SIM → C++ de eng::adv, assets y previewer
│   ├── ROADMAP_HUD.md                → HUD de juego (eng::hud): medidores, contadores, rejillas, slots, drag & drop y regiones con presupuesto
│   ├── REFACTOR_PLAYFIELD_SCROLL.md  → plan por fases del refactor playfield/scroll
│   ├── REGLAS_PIPELINE_TILES.md      → reglas de oro del pipeline de tiles
│   ├── ROADMAP_TILED_WORLD_SUPPORT.md → Tiled: parser/cooker, tilesets, capas, orientación y streaming
│   ├── ROADMAP_JUEGO_SPRITES_BOBS.md → adecuación de sprites HW/BOBs para juego: compuestos, trayectorias y mecanismos
│   ├── ROADMAP_SPRITES_COMPLETO.md   → evaluación de la consulta y plan detallado de las fases abiertas de sprites
│   ├── ROADMAP_3D_BLITTER.md          → renderer 3D de bajo nivel: clip, proyección, painter y rutas de relleno Amiga
│   ├── ROADMAP_3D_GAME_API.md         → API de juego 3D: assets, escena retenida, materiales, animación, visibilidad, colisiones y física
│   ├── PROBLEMA_LAUNCHER_DEMOS_NUEVAS.md → enunciado de un problema conocido
│   └── …
├── optimization/        → guías de optimización 68000/C++ y rendimiento
└── methodology/         → metodología, runbooks de agentes, desarrollo
    ├── LECCION-CONTEXTO-DE-LA-FUENTE.md → no obcecarse con código que falla sin la fuente
    └── …
```

## Dónde poner cada cosa
- Nuevas demos (cómo crear una): documentar en `docs/demos/effects/` y en el
  README de la demo; las plantillas de esqueleto están en cualquier demo de
  `demos/amiga/` y se describen en `docs/STRUCTURE.md` §4.
- Guías de estilo de código: `docs/engine/architecture/CODING_STYLE.md`.
- Guías de optimización: `docs/guides/optimization/`.
- Roadmaps y planes: `docs/guides/roadmap/` (marcar su estado en el `README.md` de la carpeta). La tabla única de destino de un documento nuevo está en `docs/README.md`.
- Soporte Tiled y pipeline de mundo/tilesets: `docs/guides/roadmap/ROADMAP_TILED_WORLD_SUPPORT.md`.
- Runbooks de agentes y metodología: `docs/guides/methodology/`.

## Tutoriales

- [INTENTION_API_TUTORIAL.md](INTENTION_API_TUTORIAL.md): API 2D de alto nivel por intenciones (`App`/`Game`, `SpriteDirector`, `SpriteBank`/`GameView`, plantillas y coste), con el mapa de la implementación interna para bajar de nivel.

- [3D_GAME_TUTORIAL.md](3D_GAME_TUTORIAL.md): como montar un juego 3D con primitivas o modelos cocinados, relleno Blitter, fisica de esferas/partes y presupuesto de 25 fps.

- [3D_HIGH_PERFORMANCE_GAMEPLAY.md](3D_HIGH_PERFORMANCE_GAMEPLAY.md): rutas de render medidas, física desacoplada en el hueco del Blitter, fixed y presupuesto de 25 fps para juegos 3D en A500.
