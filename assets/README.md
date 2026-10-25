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
| `amiga/sprites/abyss/abyss.bpl`, `abyss.pal`, `bob.bpl` | Assets de la demo clásica de Bartman/vscode-amiga-debug (`BartmanBasic`): imagen interleaved 320×256×5, paleta de 32 colores y hoja de 6 BOBs 32×16 enmascarados. Uso interno de prueba; la demo 213 los reproduce. |
| `amiga/audio/testmod.p61` | Módulo P61 de ejemplo ("P61.testmod" de Skylord/Sector 7) usado por las demos de música; mismo fichero que el del ejemplo de Bartman. |
| `amiga/hud/test_icons.png` | Hoja de prueba del pipeline de atlas del HUD (`tools/assets/hud-atlas.mjs`): 4 iconos 16×16 enmascarados (cuadrado, cruz, anillo, rombo) generados en el repo para validar el formato planar interleaved. Uso interno de prueba. |
| `amiga/tiles-reference/Beginning Fields.png` | Render plano (640×640) del mapa «Beginning Fields» de *The Fan-tasy Tileset (Free) 1.5.9*; se usa como referencia visual del pipeline Tiled y banco de pruebas de tiles. Revisar la licencia del tileset antes de redistribuir el arte; el proyecto Tiled fuente se lee de su ruta local. |

*Nota: `assets/amiga/tiles-reference` fue reubicado aquí en la reorganización; las
tools leen de esta ruta canónica.*