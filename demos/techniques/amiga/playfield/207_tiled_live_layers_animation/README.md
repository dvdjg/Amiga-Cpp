# 207 - Beginning Fields con capas vivas animadas

Demo T5: la base se cuece sin las celdas de `Flowers`, `Water` y `Object Layer 1`; un `TileLayerStack` compone el frame actual de las tres capas vivas. La cámara recorre todo el mapa con paradas de 3 s y las celdas que cambian se parchean por Blitter en cada parada. El contrato completo está en [`TILED_COOK_PLAN.md`](../../../../../docs/engine/architecture/TILED_COOK_PLAN.md).

## Qué demuestra

- Cook-plan validado: `Ground` es la base cocida; `Flowers`, `Water` y la fogata se emiten como overlays animados. `Road`, `Shadows`, `Object Shadows` y `RockSlopes_Auto` siguen cocidos por encima de las flores; el validador lo declara como aviso, no como error.
- La visita empieza centrada en la fogata, pasa por agua y flores, recorre las cuatro esquinas y el centro, y vuelve a la fogata. Durante el desplazamiento no se parchean las capas vivas; en cada parada de 3 s sí se actualizan.
- `TileLayerStack` compone las celdas vivas y `XLimitedPlayfield::patch_animations` redibuja solo los grupos cuyo frame cambia. El overlay se coloca en la **columna de mundo** (`wx`) de su bloque base, no en la columna visible, y la visibilidad se decide con `sx`/`sy`.
- `MapCellSaveCacheSet` guarda el fondo real: arena pequeña de 96 × 192 B (18 KB) para tiles y arena grande de 8 × 1,5 KB (12 KB) para sprites fusionados. Captura una vez, restaura antes del frame nuevo y libera la entrada al salir de la ventana; nunca reconstruye el fondo con `add_draw` ni con el tile cocinado.
- **Fusión automática**: el cooker detecta clusters rectangulares de celdas contiguas con la misma cadencia y gids de un solo uso, y los emite como **un grupo de un sprite** (la fogata 2×2 pasa a 32×32). El runtime dibuja un blit enmascarado por segmento y un único save-under; agua/flores, con gids repetidos, siguen como celdas. La decisión no conoce la fogata: cualquier mapa que cumpla las reglas entra solo. El parche recorre las capas de primer plano a fondo y reparte el resto con el cursor round-robin.
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

`selected/` contiene `map.bundle`, `tilebank.xlimited.bin` (233 KB), `animations.h`, `anim_sheet.bin` (24,8 KB) y `anim_masks.bin` (4,1 KB): 59 grupos, 220 frames y 156 celdas vivas (`Flowers` + `Water` + la fogata fusionada). Las 34 celdas de agua bajo objetos estáticos quedan cocidas.

## Medidas

- `A500_release` con visita, fusión y HUD cada 4 frames: **37,95 fps de media**, mínimo **30,51**, **170.489 ciclos/frame**, **1,202 campos/frame** (ventanas 35,38-49,53). La media supera el suelo de 25, pero el mínimo de ventana no y la cadencia no es 50 Hz: es la deuda de rendimiento abierta. La fusión de la fogata en un solo sprite y la supresión de las celdas de agua bajo objetos estáticos bajaron el coste frente a los 28,06/22,19/252.792 previos.
- Parada de agua congelada (`DEMO_TOUR_STOP=1`) con HUD cada 4 frames: **38,64 fps**, 183.603 ciclos/frame, 1,294 campos/frame. Ablación sin parche: **49,90 fps**, 142.150 ciclos/frame; el coste está en el parche, no en el scroll ni en el HUD.
- El avance circular sin `%` por grupo (`__umodsi3`), el job único por celda (todos los planos) y refrescar el HUD cada 4 frames subieron la parada de 18,18 a 35,86 fps. El tope de 24 jobs reparte las celdas nuevas sin romper la fogata.
- Secuencia de la parada de la fogata (8 frames): la fogata cambia de silueta con el anillo de piedras y la hierba estables; las flores blancas/rojas y el agua cambian sus frames. Diff de región estática = 0; los recortes 4× no muestran restos ni bloques negros/rojos.
- La revisión canónica con Ollama describe la escena como estática: es un falso negativo global sobre detalles de 16 px. La evidencia canónica es la secuencia + diff de píxeles y los recortes por entidad.

## Alcance y deuda

- `Flowers` viva convive con `Road/Shadows/RockSlopes` cocidos por encima; una celda de flor que quedara bajo esas capas podría ser tapada. El validador lo avisa. En esta ventana las flores visibles están sobre hierba y no se observa solape incorrecto.
- El save-under por celda está implementado en `MapCellSaveCacheSet` (arenas pequeña/grande, captura única, restore antes del frame, release al salir). No se usa el mapa cocinado para el fondo.
- **Solape estático**: el cooker detecta celdas animadas cubiertas por un objeto estático posterior (p. ej. la copa de un árbol sobre el agua) y no las emite como animadas. El bake incluye el primer frame de las capas animadas (`render.bake_animated_first_frame`), de modo que esas celdas conservan agua + objeto sin que el parche las borre. Se suprimieron 34 celdas de agua y varias flores bajo objetos; la copa del árbol de la izquierda deja de comerse. El criterio es conservador: capas de tiles estáticas con transparencia no suprimen celdas completas, solo objetos/sprites.
- La **caché background+frame** por celda/frame no se usa: para flores/agua costaría ~244 KB Chip (28+127 celdas × 8 frames × 192 B) y el fondo podría quedar obsoleto si otra capa lo tapa. La **fusión de frames** sí se usa para objetos compuestos únicos (fogata), que no duplica el banco porque sus gids no aparecen en otras celdas. La comparación completa (objeto libre, bounding boxes, blit mayor) está en [`TILED_MAP_RUNTIME.md` §5.4.1](../../../../../docs/engine/architecture/TILED_MAP_RUNTIME.md#541-objetos-libres-y-caché-precompuesta-qué-aplica-y-qué-no).
- El **fps** de las paradas es la deuda principal: la media sube a 41,61 pero el mínimo de ventana sigue en 35,38 y sin 50 Hz estables. La parada de la fogata mide 24,32 fps (291.713 ciclos, 2,056 campos); el agua 38,64 y las flores 42,79. Sin parche el trabajo real es W≈11k ciclos (M≈63k); los 142k de la medida son el suelo de VBlank. Con flores el peor frame es M≈403k y con fogata M≈679k: ese burst es el que rompe 50 Hz. La ruta de doble buffer opcional (DEMO_DOUBLE_BUFFER=1, 256 KB) mide 11,10 fps en A1200 frente a 15,99 del mismo build sin doble buffer: no se activa por defecto. El bucle usa `Engine::run_frames` con `backend.takeover_system()`: el latido llega por IRQ de VBlank y la espera es `Wait()`, no sondeo activo. El pase `Occluder`/`after_actors` se valida en la demo 208.

## Telemetría opcional

La build normal no paga instrumentación. Para diagnosticar operaciones de Blitter y fases:

```bash
EXTRA_DEFINES="-DDEMO_BLIT_STATS=1" bash tools/build/build-demo.sh <demo> --release
EXTRA_DEFINES="-DDEMO_PROF=1" bash tools/build/build-demo.sh <demo> --release
```

`DEMO_BLIT_STATS` añade al HUD `J` (jobs), `B0/B1/B2` (celdas parcheadas por capa) y `S/F` (entradas y fallos del `MapCellSaveCache`); `DEMO_PROF` instrumenta las secciones de update/execute/compose para `tools/debug/profile.mjs`. `DEMO_TOUR_STOP=N` congela la visita en una parada concreta (0=fogata, 1=agua, 2=flores, 3.. esquinas/centro) para capturar su secuencia sin esperar el recorrido. El código queda fuera cuando no se define, por lo que la build normal no tiene coste adicional.

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
