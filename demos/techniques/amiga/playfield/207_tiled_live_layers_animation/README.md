# 207 - Beginning Fields con capas vivas animadas

Demo T5: la base se cuece sin las celdas de `Flowers`, `Water` y `Object Layer 1`; un `TileLayerStack` compone el frame actual de las tres capas vivas. La cámara recorre todo el mapa con paradas de 3 s y las celdas que cambian se parchean por Blitter en cada parada. El contrato completo está en [`TILED_COOK_PLAN.md`](../../../../../docs/engine/architecture/TILED_COOK_PLAN.md).

## Qué demuestra

- Cook-plan validado: `Ground` es la base cocida; `Flowers`, `Water` y la fogata se emiten como overlays animados. `Road`, `Shadows`, `Object Shadows` y `RockSlopes_Auto` siguen cocidos por encima de las flores; el validador lo declara como aviso, no como error.
- La visita empieza centrada en la fogata, pasa por agua y flores, recorre las cuatro esquinas y el centro, y vuelve a la fogata. Durante el desplazamiento no se parchean las capas vivas; en cada parada de 3 s sí se actualizan.
- `TileLayerStack` compone las celdas vivas y `XLimitedPlayfield::patch_animations` redibuja solo los grupos cuyo frame cambia. El parche usa el mismo dibujo de grupo que la pasada inicial y reparte grupos entre frames si el plan se llena; los grupos pendientes conservan su marca `changed`.
- `TileAnimationPriority` ordena `Flowers` (fondo), `Water` (decorado) y la fogata (foreground). `kBudgetedAnimations=false` mantiene las tres activas para la validación funcional.

## Pipeline

```bash
node tools/tiled/validate-cook-plan.mjs \
  "../../../Assets/2D/The Fan-tasy Tileset (Free) 1.5.9/Tiled/Tilemaps/Beginning Fields.tmx" \
  tools/tiled/fixtures/beginning-fields.live-flowers.cook-plan.json

node tools/tiled/cook-map.mjs \
  "../../../Assets/2D/The Fan-tasy Tileset (Free) 1.5.9/Tiled/Tilemaps/Beginning Fields.tmx" \
  --plan tools/tiled/fixtures/beginning-fields.live-flowers.cook-plan.json \
  --profiles ehb --dither none \
  --out out/assets/tiled/beginning-fields-live-flowers --select ehb

bash tools/build/build-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --release
bash tools/run/run-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --config A500_release --sequence-frames 12 --sequence-interval-ms 250
```

Presets:

- `beginning-fields.live-flowers.cook-plan.json`: flores, agua y fogata vivas; es el que usa esta demo.
- `beginning-fields.live.cook-plan.json`: agua y fogata vivas; flores cocidas.
- `beginning-fields.fire.cook-plan.json`: solo la fogata viva.
- `beginning-fields.baked.cook-plan.json`: todo cocido.

La paleta la genera el cooker a partir de la composición completa del mapa (incluye capas animadas y objetos, no solo el bake) y se cuantiza a 32 bases EHB con `quantize-ehb.mjs`; no se usa una paleta fija del repositorio.

`selected/` contiene `map.bundle`, `tilebank.xlimited.bin` (218 KB), `animations.h`, `anim_sheet.bin` (47,6 KB) y `anim_masks.bin` (7,9 KB): 76 grupos, 248 frames y 227 celdas vivas (`Flowers` + `Water` + fogata).

## Medidas

- `A500_release` con visita: **40,53 fps**, **175.028 ciclos/frame**, **1,234 campos/frame**; ventanas 34,04-47,54 fps. El viaje va por encima del suelo de 25 fps y las paradas concentran el parcheo; el reparto usa grupos pequeños, 18 jobs y repinta el bloque base de la fogata.
- La secuencia de 60 frames a 250 ms termina con estado READY y muestra la visita completa: fogata, agua, casa/esquinas y vuelta. El frame-diff incluye el scroll; no hay huecos negros ni frames congelados.
- La revisión canónica con Ollama describe la escena como estática: es un falso negativo global sobre detalles de 16 px. La evidencia canónica es la secuencia + diff de píxeles (y el recorte de agua), no la descripción del VLM; queda pendiente una pasada por entidad con recortes separados.

## Alcance y deuda

- `Flowers` viva convive con `Road/Shadows/RockSlopes` cocidos por encima; una celda de flor que quedara bajo esas capas podría ser tapada. El validador lo avisa. En esta ventana las flores visibles están sobre hierba y no se observa solape incorrecto.
- El **fps** del scroll es la deuda principal; las paradas ya demuestran las tres animaciones y el viaje se mantiene por encima de 25 fps.
- El pase `Occluder`/`after_actors` se valida en la demo 208.

## Telemetría opcional

La build normal no paga instrumentación. Para diagnosticar operaciones de Blitter y fases:

```bash
EXTRA_DEFINES="-DDEMO_BLIT_STATS=1" bash tools/build/build-demo.sh <demo> --release
EXTRA_DEFINES="-DDEMO_PROF=1" bash tools/build/build-demo.sh <demo> --release
```

`DEMO_BLIT_STATS` añade al HUD `J` (jobs), `W` (palabras) y los contadores de props/actores/occluders; `DEMO_PROF` instrumenta las secciones de update/execute/compose para `tools/debug/profile.mjs`. El código queda fuera cuando no se define, por lo que la build normal no tiene coste adicional.

```bash
bash tools/run/run-demo.sh <demo> --config A500_demo_blit_stats1_release \
  --telemetry-samples 8 --telemetry-interval-ms 100
node tools/debug/blitter-report.mjs <demo> A500_demo_blit_stats1_release
```

El informe muestra jobs, palabras, palabras de Copper y el extra de fillup por frame; el runner ya lo deja también en `run-report.json`.

La telemetría general (`-DENG_METRICS=1`) añade `TilePatchStats`: grupos recorridos, repintados y diferidos, celdas pintadas/descartadas y bloques base restaurados. Es la lectura recomendada antes de ajustar el reparto de trabajos:

```bash
EXTRA_DEFINES="-DENG_METRICS=1" bash tools/build/build-demo.sh <demo> --release
bash tools/run/run-demo.sh <demo> --config A500_eng_metrics1_release \
  --telemetry-samples 8 --telemetry-interval-ms 100
node tools/debug/metrics-report.mjs <demo> A500_eng_metrics1_release
```
