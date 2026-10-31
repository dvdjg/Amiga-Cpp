# 207 - Beginning Fields con capas vivas animadas

Demo T5: la base se cuece sin las celdas de `Flowers`, `Water` y `Object Layer 1`; un `TileLayerStack` compone el frame actual de las tres capas vivas. La cámara recorre todo el mapa con paradas de 3 s y las celdas que cambian se parchean por Blitter en cada parada. El contrato completo está en [`TILED_COOK_PLAN.md`](../../../../../docs/engine/architecture/TILED_COOK_PLAN.md).

## Qué demuestra

- Cook-plan validado: `Ground` es la base cocida; `Flowers`, `Water` y la fogata se emiten como overlays animados. `Road`, `Shadows`, `Object Shadows` y `RockSlopes_Auto` siguen cocidos por encima de las flores; el validador lo declara como aviso, no como error.
- **Ninguna celda animada visible queda congelada.** Las 61 celdas de `Flowers` y las 155 de `Water` se animan. Cuando un **objeto opaco** de una capa superior (la banca junto a la fogata) cubre píxeles de una celda viva, el cooker emite una **capa de cobertura** con esos píxeles; el runtime la reestampa justo después de repintar la celda, así que el objeto sigue por encima del overlay. Las **sombras** cuentan para decidir si la celda se cuece (si la tapan casi entera, se conserva la sombra horneada) pero **no se reestampan**: reestampar el sprite de la sombra la convertía en un bloque sólido (la cuña oscura junto al tocón). El solape de borde ≤2 px se tolera. Las 7 celdas que quedan casi totalmente tapadas (<25 % de su silueta visible) se cuecen: su animación no se vería y ahorran celda viva, cobertura y jobs.
- La visita empieza centrada en la fogata, pasa por agua y flores, recorre las cuatro esquinas y el centro, y vuelve a la fogata. Durante el desplazamiento no se parchean las capas vivas; en cada parada de 3 s sí se actualizan.
- `TileLayerStack` compone las celdas vivas y `XLimitedPlayfield::patch_animations` redibuja solo los grupos cuyo frame cambia. El overlay se coloca en la **columna de mundo** (`wx`) de su bloque base, no en la columna visible, y la visibilidad se decide con `sx`/`sy`.
- `MapCellSaveCacheSet` guarda el fondo real: arena pequeña de 96 × 192 B (18 KB) para tiles y arena grande de 8 × 1,5 KB (12 KB) para sprites fusionados. Captura una vez, restaura antes del frame nuevo y libera la entrada al salir de la ventana. **El agua no la usa**: sus grupos tienen la misma máscara en todos los frames (`kTileAnimationFlagStableMask`), el cookie-cut conserva el fondo por sí solo y se repinta con un solo blit. Flores y fogata, con máscara variable, mantienen save-under.
- **Fusión automática**: el cooker detecta clusters rectangulares de celdas contiguas con la misma cadencia y gids de un solo uso, y los emite como **un grupo de un sprite** (la fogata 2×2 pasa a 32×32). El runtime dibuja un blit enmascarado por segmento y un único save-under. La decisión no conoce la fogata: cualquier mapa que cumpla las reglas entra solo.
- **Visibilidad cacheada**: la posición de una celda viva es fija en el mundo; su visibilidad solo cambia al mover la cámara. `rebuild_anim_visible` construye una tabla `grupo → celdas visibles (celda, target_x, ring_y)` y el parche por frame la reutiliza en vez de recalcular `wx/wy/ring_y` de todas las celdas de cada grupo cambiado.
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

La paleta la genera el cooker a partir de la composición completa del mapa (incluye capas animadas y objetos, no solo el bake) y la cuantiza a 32 bases EHB con `quantize-ehb.mjs`; no se usa una paleta fija del repositorio.

`selected/` contiene `map.bundle`, `tilebank.xlimited.bin` (233 KB), `animations.h`, `anim_sheet.bin` (50,8 KB) y `anim_masks.bin` (6,1 KB): 98 grupos, 247 frames y 244 celdas (61 de `Flowers`, 155 de `Water`, la fogata fusionada y 27 coberturas de objetos opacos). Las coberturas cuestan un frame de 16×16 + máscara por celda y solo se dibujan cuando su celda se repinta; las máscaras de silueta fija se guardan una sola vez.

## Cierre de rendimiento 2026-10-31

La política de capas cambió para eliminar el parpadeo y el coste del parche:

- **Scroll:** no se parchea ninguna capa viva; el movimiento es hardware y el frame vuelve a ~50 fps.
- **Parada:** solo se anima la capa protagonista (fogata, agua o flores). Las demás conservan su último frame; no se repintan.
- El parche de la capa protagonista se ejecuta cada 5 frames y el HUD cada 8.

| Caso | fps | Ciclos/frame | Campos/frame |
|---|---:|---:|---:|
| Visita completa | **49,52** | 143.238 | 1,010 |
| Parada de la fogata (`DEMO_TOUR_STOP=0`) | **49,97** | 141.965 | 1,001 |
| Base sin capas vivas (`DEMO_NO_LIVE=1`) | 49,87 | 142.243 | 1,003 |

La secuencia de 30 frames no muestra bandas ni parpadeo; la fogata cambia de frame (diff en la zona central). La visita completa se queda a 1,010 campos: le faltan ~1,4k ciclos para el campo exacto, no hay un defecto visual.

## Medidas

Medidas con `measure-fps.mjs` sobre `A500_release`, build del árbol de trabajo sobre el commit `33882349`, 2026-10-29.

- Visita completa: **39,90 fps de media**, mínimo de ventana **35,4**, máximo 46,0, **177.803 ciclos/frame**, **1,253 campos/frame** (8 ventanas). La cadencia es variable por diseño (paradas y desplazamiento); la media no se usa como presupuesto. La visita incluye la **fogata animándose también durante el scroll** (parche de la capa 2 cada 5 frames); sin ese parche la misma visita medía 47,60 fps (mínimo 44,0). Las flores y el agua se quedan en su último frame durante el desplazamiento.
- Parada de la fogata (`DEMO_TOUR_STOP=0`): **44,09 fps**, 160.900 ciclos/frame, 1,134 campos/frame, ventanas 43,2–45,6. La política de degradación por trabajo congela el agua (capa ambiente) mientras la fogata y las flores siguen a 50; con `DEMO_NO_BUDGET` (tope fijo de 8 jobs) la misma parada mide **37,41 fps**. El anillo de `set_tick` subió la parada fija de 35,7 a 37,4.
- Parada de agua (`DEMO_TOUR_STOP=1`): **48,58 fps**, 146.009 ciclos/frame, 1,029 campos/frame.
- Parada de flores (`DEMO_TOUR_STOP=2`): **48,32 fps**, 146.819 ciclos/frame, 1,035 campos/frame.
- El objetivo de 50 Hz queda abierto en la fogata y en la peor ventana de la visita (**35,4**); agua y flores están a ~48,5. En la fogata el HUD de régimen lee `W=120.004` y `M=206.970`; el pico sigue siendo la llegada con el redraw completo, no el régimen.
- **Fogata durante el scroll**: la capa 2 se parchea cada 5 frames (su arte cambia cada 5 ticks, ~10 fps) y las capas 0/1 se desactivan; la secuencia final de Ollama confirma que la fogata cambia de llama mientras el agua y las flores permanecen quietas, sin huecos ni bloques oscuros. La carrera con el haz en buffer único es el siguiente frente: ver [`beam-safe-bobs.md`](../../../../../docs/reference/amiga/techniques/beam-safe-bobs.md).
- **Interleaved de un solo blit (experimento)**: el banco mixto (agua planar, flores/fogata/coberturas interleaved) dio 43,48 fps en la fogata y 47,60 en la visita, sin mejora frente al planar (44,09/47,60) y con 32,8 KB más de hoja; el interleaved completo (104 KB) no deja contiguos los 104 KB de pool en el A500 de 512K. Queda como opción explícita `animations.interleaved` en el plan de cocina, por defecto apagada.
- **Opacidad por frame y clusters**: el campo `opaque_mask` está implementado (frame opaco = copia sin restore), pero en este arte los frames opacos pertenecen a grupos ya opacos; no hay ganancia medible. Los clusters de restore no aplican: el agua es de silueta fija (sin restore) y las flores vivas no son adyacentes.

## Alcance y deuda

- **Cobertura de objetos**: la capa `Covers` se emite solo para **objetos opacos** que solapan más de 2 px con el overlay y cuando la celda conserva al menos el 25 % de su silueta visible. Se compone por píxel (alfa > 0) y no por bounding box. Las **sombras** cuentan para la regla de visibilidad (si tapan casi toda la celda, se cuece para conservar la sombra horneada) pero no se reestampan; así no aparece la cuña sólida que dejaba el sprite de sombra. La cobertura se dibuja tras la celda que tapa, no como capa normal: un job extra solo en el frame que repinta esa celda.
- **Silueta fija**: el agua se repinta con 1 blit y sin save-under porque su máscara no cambia entre frames. Es una propiedad del arte, no de la capa; el cooker la deduce comparando las máscaras y la registra en `animations.json`.
- **Save-under y fondo**: el fondo real de cada celda con máscara variable se captura una vez en `MapCellSaveCacheSet`; nunca se reconstruye con `add_draw` ni con el tile del bake. La captura se libera al salir de la ventana (sin restaurar: el scroll repinta la columna).
- **Solape estático**: el bake incluye el primer frame de las capas animadas (`render.bake_animated_first_frame`), de modo que las celdas que quedan bajo un objeto conservan agua/flores + objeto mientras la cobertura no se repinta. El criterio es de visibilidad: si el solape es de borde (≤2 px) se anima la celda aunque el objeto pierda un par de píxeles al repintarse; si el objeto o la sombra tapan >75 % de la silueta se cuece la celda (ahorra celda viva, cobertura y jobs).
- **Artefactos oscuros**: el save-under de un solo blit y la cobertura de solo objetos opacos se validan con el pase de artefactos base-vs-vivo (`DEMO_NO_LIVE=1` + `tools/analyze/dark-clusters.mjs`) descrito en `DEMO_VISUAL_DEBUG.md` §6.4.4. Es el gate que cazó la cuña de 196 px junto al tocón que la visión canónica no vio.
- El **fps** sigue siendo la deuda principal: la visita baja a **35,4** fps en su ventana más lenta (39,9 de media) por la fogata animada durante el scroll, y la fogata se queda en **44,1**; agua y flores están a ~48,5. El presupuesto por capas con histéresis (agua primero), el parche de la fogata en scroll y el anillo de vencimientos de `set_tick` ya están aplicados; quedan el deadline/prioridad a nivel de engine, el **scheduler de BOBs por deadlines de haz** ([`beam-safe-bobs.md`](../../../../../docs/reference/amiga/techniques/beam-safe-bobs.md)) para eliminar el flicker ocasional, el canal C del Blitter y el doble buffer opcional (`DEMO_DOUBLE_BUFFER=1`, 256 KB), que sigue sin compensar. El catálogo y los resultados de la campaña están en [`OPTIMIZACION_TILED_ANIMADO.md`](../../../../../docs/guides/optimization/OPTIMIZACION_TILED_ANIMADO.md) y el diseño pendiente en [`PRESUPUESTO_PRIORITARIO_FRAME.md`](../../../../../docs/guides/optimization/PRESUPUESTO_PRIORITARIO_FRAME.md).
- La **evaluación canónica de visión** (`src/VISION.md`, prompt sin pistas) describe la escena como estática: es un falso negativo sobre detalles de 16 px. La evidencia que prevalece es la secuencia + `frame-diff` + los recortes por entidad; la validación dirigida con Ollama confirma que las flores cambian y que la banca queda idéntica. La secuencia de 10 capturas de la visita final (fogata en scroll) responde que la fogata sigue animándose, el agua y las flores quedan quietas y no hay huecos ni bloques oscuros.

## Telemetría opcional

La build normal no paga instrumentación. Para diagnosticar operaciones de Blitter y fases:

```bash
EXTRA_DEFINES="-DDEMO_BLIT_STATS=1" bash tools/build/build-demo.sh <demo> --release
EXTRA_DEFINES="-DDEMO_PROF=1" bash tools/build/build-demo.sh <demo> --release
```

`DEMO_BLIT_STATS` añade al HUD `J` (jobs), `B0/B1/B2` (celdas parcheadas por capa) y `S/F` (entradas y fallos del `MapCellSaveCache`); `DEMO_PROF` instrumenta las secciones de update/execute/compose para `tools/debug/profile.mjs`. `DEMO_TOUR_STOP=N` congela la visita en una parada concreta (0=fogata, 1=agua, 2=flores, 3.. esquinas/centro) para capturar su secuencia sin esperar el recorrido. `DEMO_NO_COVER=1` desactiva la capa de cobertura, `DEMO_NO_LIVE=1` deja solo la base cocida (referencia para el pase de artefactos base-vs-vivo) y `DEMO_NO_BUDGET=1` usa el tope fijo de 8 jobs sin la degradación por capas (referencia A/B). El código queda fuera cuando no se define, por lo que la build normal no tiene coste adicional.

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
