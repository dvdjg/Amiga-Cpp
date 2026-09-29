# Técnicas: Amiga · playfield

Demos de **playfield** (técnicas de hardware Amiga). Índice: [../../../README.md](../../../README.md).
Estructura y numeración: [../../../../docs/STRUCTURE.md](../../../../docs/STRUCTURE.md) §4 y
[../../../../docs/ai-dev-environment/NUMBERING.md](../../../../docs/ai-dev-environment/NUMBERING.md).

## Catálogo

| Demo |
|---|
| [100_virtual_tile_scene_scroll](100_virtual_tile_scene_scroll/README.md) |
| [101_ehb_tile_scroll_driver](101_ehb_tile_scroll_driver/README.md) |
| [103_tile_scroll_ring](103_tile_scroll_ring/README.md) |
| [104_tile_scroll_ring_dualpf](104_tile_scroll_ring_dualpf/README.md) |
| [105_tile_scroll_xyunlimited_dualpf](105_tile_scroll_xyunlimited_dualpf/README.md) |
| [107_xlimited_corkscrew](107_xlimited_corkscrew/README.md) |
| [110_ylimited_shooter](110_ylimited_shooter/README.md) |
| [111_xlimited_sidescroller](111_xlimited_sidescroller/README.md) |
| [112_xlimited_robocod](112_xlimited_robocod/README.md) |
| [113_mode_switch](113_mode_switch/README.md) |
| [114_mode_switch_bands](114_mode_switch_bands/README.md) |
| [115_mode_switch_ehb_hud](115_mode_switch_ehb_hud/README.md) |
| [120_virtual_playfield](120_virtual_playfield/README.md) |
| [121_mirror_scroll](121_mirror_scroll/README.md) |
| [122_doublebuffer_scroll](122_doublebuffer_scroll/README.md) |
| [125_layers_dualpf](125_layers_dualpf/README.md) |
| [201_ehb_map](201_ehb_map/README.md) |
| [202_xlimited_dpf](202_xlimited_dpf/README.md) |

## Build / run / analyze

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/<NNN>_<tema> --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/<NNN>_<tema>
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/playfield/<NNN>_<tema>
```

El número `NNN` es único **dentro de este ámbito** (`demos/techniques/amiga/playfield`).
