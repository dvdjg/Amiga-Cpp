# Técnicas: Amiga · Copper

Demos de **Copper** (listas de cobre, copper chunky, splits por línea, planificación de escena).
Son **técnicas de hardware**: usan el vocabulario de chipset de Amiga, no son portables.

Índice general de demos: [`../../../README.md`](../../../README.md). Estructura y numeración:
[`../../../../docs/STRUCTURE.md`](../../../../docs/STRUCTURE.md) §4 y
[`../../../../docs/ai-dev-environment/NUMBERING.md`](../../../../docs/ai-dev-environment/NUMBERING.md).

## Catálogo

| Demo | Qué enseña |
|---|---|
| [020_copper_basic](020_copper_basic/README.md) | Copperlist básica en Chip RAM (bandas de color). |
| [030_ehb_palette_zones](030_ehb_palette_zones/README.md) | Paletas EHB por zonas (6 planos, `BPL1..BPL6`). |
| [055_copper_rainbow](055_copper_rainbow/README.md) | Rainbow de Copper (gradiente por línea). |
| [085_copper_plan_scene](085_copper_plan_scene/README.md) | `copper::Plan` dentro de `scene::compose` (composición por etapas). |

## Build / run / analyze

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/copper/020_copper_basic --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/copper/020_copper_basic
bash ./tools/analyze/analyze-demo.sh demos/techniques/amiga/copper/020_copper_basic
```

El número `NNN` es único **dentro de este ámbito** (`demos/techniques/amiga/copper`); el siguiente
libre se consulta con `node tools/check/next-number.mjs demos/techniques/amiga/copper`.
