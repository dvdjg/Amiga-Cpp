# Documentación de demos y efectos

Documentación de las **demos del engine** y de los **efectos/técnicas que
enseñan**, por plataforma. Organización canónica en `docs/STRUCTURE.md` §10.

```
docs/demos/
├── effects/              → efectos y técnicas de demoscene (qué enseña cada demo)
│   ├── demoscene-effects-integration.md
│   ├── DEMOSCENE_EFFECT_REPLICATION_POLICY.md
│   ├── DEMOSCENE_REPO_INDEX.md
│   └── …
└── tile-pipeline/        → pipeline de assets/tiles/EHB
    ├── PIPELINE_TILES_EHB.md   → pipeline cuantización→tiles→EHB→escena
    ├── ai-reports/             → informes de verificación IA de los pipelines
    └── …
```

Las demos en sí viven en `demos/<plataforma>/<NNN>_<tema>/`, y cada una
documenta en su `README.md` qué invariantes enseña y cómo validarla.
La documentación de cada efecto concreto suele estar en el `README.md` de la
demo correspondiente (`demos/amiga/`).

## Dónde poner documentación nueva de efectos/demos
- Efecto nuevo demostrado por una demo: en el `README.md` de la demo.
- Patrón técnico reutilizable: `docs/reference/amiga/techniques/`.
- Resultado de una verificación de pipeline (informes IA): `docs/demos/tile-pipeline/ai-reports/`.