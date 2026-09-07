# `assets/` — assets fuente

Assets **fuente**: editables, con licencia y listos para alimentar un pipeline
(cuantización, tiling, sprites, audio). No se escribe aquí automáticamente: los
pipelines **leen** de aquí y **escriben** en `out/assets/<pipeline>/…`.

La especificación completa de organización está en `docs/STRUCTURE.md` (§5).

## Estructura

```
assets/
├── amiga/
│   ├── tiles-reference/  → imágenes de referencia reales para probar el pipeline EHB/tiles
│   ├── sprites/          → sprites/bobs fuente
│   ├── audio/            → módulos/efectos fuente
│   └── maps/             → mapas editables (TMX, planimetrías, …)
├── atarist/              → (futuro) assets Atari ST
└── megadrive/            → (futuro) assets Megadrive
```

## Reglas

- Mismo esquema por plataforma: `assets/<plataforma>/<dominio>/`.
- Documenta la **procedencia y licencia** de cada fuente en el README del
  subdirectorio o en este README (añade sección con la trazabilidad).
- Nunca pongas aquí salidas generadas (`out/assets/` es su sitio).
- Los assets canónicos de un pipeline que deban congelarse van a `artifacts/` con
  un commit explícito.

## Trazabilidad de las fuentes actuales

| Archivo | Origen / licencia |
|---|---|
| `amiga/tiles-reference/real/pac_man.jpg`, `apple_guy.jpg`, `aussie_bum.jpg`, `forgotten_relict.jpg`, `landscape_painting.jpg`, `metalslug.png` | Fan-art/ilustraciones usadas como banco de pruebas del pipeline; propiedad de sus autores (uso interno de prueba). |
| `amiga/tiles-reference/real_640/*` | Redimensiones Lanczos a ~640px usadas como referencia del pipeline (generadas a partir de las fuentes anteriores). |

*Nota: `assets/amiga/tiles-reference` fue reubicado aquí en la reorganización; las
tools leen de esta ruta canónica.*