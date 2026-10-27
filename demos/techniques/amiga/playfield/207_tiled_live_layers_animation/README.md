# 207 - Beginning Fields con capas vivas animadas

Demo T5: la base se cuece sin las celdas de `Flowers`, `Water` y `Object Layer 1`; un `TileLayerStack` compone el frame actual de las tres capas vivas sobre una cámara fija. Las celdas que cambian se parchean por Blitter. El contrato completo está en [`TILED_COOK_PLAN.md`](../../../../../docs/engine/architecture/TILED_COOK_PLAN.md).

## Qué demuestra

- Cook-plan validado: `Ground` es la base cocida; `Flowers`, `Water` y la fogata se emiten como overlays animados. `Road`, `Shadows`, `Object Shadows` y `RockSlopes_Auto` siguen cocidos por encima de las flores; el validador lo declara como aviso, no como error.
- La cámara queda fija en `Y=300`, con agua, flores y fogata en la misma ventana. Así se valida la animación sin confundirla con el scroll.
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
  --palette tools/tiled/fixtures/beginning-fields.live-flowers.palette.json \
  --planes 6 --out out/assets/tiled/beginning-fields-live-flowers --select custom

bash tools/build/build-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --release
bash tools/run/run-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --config A500_release --sequence-frames 12 --sequence-interval-ms 250
```

Presets:

- `beginning-fields.live-flowers.cook-plan.json`: flores, agua y fogata vivas; es el que usa esta demo.
- `beginning-fields.live.cook-plan.json`: agua y fogata vivas; flores cocidas.
- `beginning-fields.fire.cook-plan.json`: solo la fogata viva.
- `beginning-fields.baked.cook-plan.json`: todo cocido.

`selected/` contiene `map.bundle`, `tilebank.xlimited.bin` (218 KB), `animations.h`, `anim_sheet.bin` (47,6 KB) y `anim_masks.bin` (7,9 KB): 76 grupos, 248 frames y 227 celdas vivas (`Flowers` + `Water` + fogata).

## Medidas

- `A500_release`: **24,25 fps**, **292.482 ciclos/frame**, **2,062 campos/frame**; ventana 22,19-24,96 fps. La cadencia queda en el borde del suelo de 25 fps; no se declara 50 fps. El parche reparte grupos pequeños (máx. 4 celdas) en round-robin por capa, limita cada frame a 24 jobs y repinta el bloque base de la capa de la fogata antes del frame enmascarado.
- La secuencia de 12 frames a 250 ms termina con estado READY. El frame-diff objetivo entre f0000 y f0006 detecta 976 px cambiados en el recorte de flores, 976 px en la fogata y 1.236 px en el campo; en la misma secuencia el agua cambia en su recorte. No hay huecos negros.
- La revisión con Ollama a tamaño completo tiende a decir que solo cambia la fogata; con un recorte 3x detecta las ondas del agua. En el recorte 4x de flores el modelo dice que son estáticas pese a que sus píxeles cambian; se registra como falso negativo del modelo, no como fallo del overlay. La evidencia canónica es la secuencia + diff de píxeles, no una única descripción del VLM.

## Alcance y deuda

- `Flowers` viva convive con `Road/Shadows/RockSlopes` cocidos por encima; una celda de flor que quedara bajo esas capas podría ser tapada. El validador lo avisa. En esta ventana las flores visibles están sobre hierba y no se observa solape incorrecto.
- El **fps** es la deuda principal. Antes de optimizar, la demo ya demuestra las tres animaciones con cámara fija.
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
