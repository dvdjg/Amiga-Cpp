# Técnicas: Amiga · blitter

Demos de **blitter** (técnicas de hardware Amiga). Índice: [../../../README.md](../../../README.md).
Estructura y numeración: [../../../../docs/STRUCTURE.md](../../../../docs/STRUCTURE.md) §4 y
[../../../../docs/ai-dev-environment/NUMBERING.md](../../../../docs/ai-dev-environment/NUMBERING.md).

## Catálogo

| Demo |
|---|
| [050_blitter_bobs](050_blitter_bobs/README.md) |
| [051_blitter_shifted_bobs](051_blitter_shifted_bobs/README.md) |
| [052_tile_staging_blits](052_tile_staging_blits/README.md) |
| [086_bob_objects](086_bob_objects/README.md) |
| [087_sprite_horizontal_rearm](087_sprite_horizontal_rearm/README.md) |
| [208_blitter_memcpy](208_blitter_memcpy/README.md) |
| [203_polygon_planes](203_polygon_planes/README.md) |
| [204_collide_game](204_collide_game/README.md) |
| [210_copper_blitter](210_copper_blitter/README.md) |

## Build / run / analyze

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/<NNN>_<tema> --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/<NNN>_<tema>
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/blitter/<NNN>_<tema>
```

El número `NNN` es único **dentro de este ámbito** (`demos/techniques/amiga/blitter`).
