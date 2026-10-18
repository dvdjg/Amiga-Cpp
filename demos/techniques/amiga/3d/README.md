# Técnicas: Amiga · 3d

Demos de **3d** (técnicas de hardware Amiga). Índice: [../../../README.md](../../../README.md).
Estructura y numeración: [../../../../docs/STRUCTURE.md](../../../../docs/STRUCTURE.md) §4 y
[../../../../docs/ai-dev-environment/NUMBERING.md](../../../../docs/ai-dev-environment/NUMBERING.md).

## Catálogo

| Demo |
|---|
| [077_math3d_cube](077_math3d_cube/README.md) |
| [078_math3d_solid](078_math3d_solid/README.md) |
| [079_wireframe](079_wireframe/README.md) |
| [080_camera_pan_window](080_camera_pan_window/README.md) (ABIERTA: contrato ✓, cadencia F1 pendiente) |
| [084_mf_rotation](084_mf_rotation/README.md) |

## Build / run / analyze

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/<NNN>_<tema> --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/<NNN>_<tema>
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/3d/<NNN>_<tema>
```

El número `NNN` es único **dentro de este ámbito** (`demos/techniques/amiga/3d`).
| 085 | [fill_clip_probe](085_fill_clip_probe/README.md) | Sonda mínima (WIP) de **recorte 2D + relleno por plano** (`clip_polygon` + `fill_polygons_by_plane`): triángulo que cruza el borde, pentágono medio fuera y cuadrado de referencia; A/B contra la referencia CPU (`-DCLIP_PROBE_CPU`). Encontró y fijó el bug de cierre del recorte (HOST-479) y la convención de rect inclusivo (`width−1`/`height−1`). |
