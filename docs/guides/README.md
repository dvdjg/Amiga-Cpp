# Guías del proyecto

Instrucciones y guías de proceso del proyecto: roadmaps vigentes, optimización,
metodología de trabajo y plantillas. Organización canónica en
`docs/STRUCTURE.md` §10.

```
docs/guides/
├── roadmap/             → ROADMAP_VIGENTE (ROADMAP_UNIFICADO.md), planes y decisiones
│   ├── ROADMAP_UNIFICADO.md          → estado del engine/demos y próximas direcciones
│   ├── REGLAS_PIPELINE_TILES.md      → reglas de oro del pipeline de tiles
│   ├── PROBLEMA_LAUNCHER_DEMOS_NUEVAS.md → enunciado de un problema conocido
│   └── …
├── optimization/        → guías de optimización 68000/C++ y rendimiento
└── methodology/         → metodología, runbooks de agentes, desarrollo
```

## Dónde poner cada cosa
- Nuevas demos (cómo crear una): documentar en `docs/demos/effects/` y en el
  README de la demo; las plantillas de esqueleto están en cualquier demo de
  `demos/amiga/` y se describen en `docs/STRUCTURE.md` §4.
- Guías de estilo de código: `docs/engine/architecture/CODING_STYLE.md`.
- Guías de optimización: `docs/guides/optimization/`.
- Roadmaps y planes: `docs/guides/roadmap/`.
- Runbooks de agentes y metodología: `docs/guides/methodology/`.