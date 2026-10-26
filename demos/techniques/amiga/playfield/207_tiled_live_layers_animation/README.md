# 207 — Beginning Fields con capas vivas animadas

Demo T5: la base se cuece **sin** las celdas vivas y un `TileLayerStack` compone el frame actual de `Water` y `Object Layer 1` (fogata) en la banda entrante. Las celdas que cambian se parchean por Blitter. `Flowers` queda cocida porque está por debajo de capas que no son vivas; el contrato completo está en [`TILED_COOK_PLAN.md`](../../../../../docs/engine/architecture/TILED_COOK_PLAN.md).

## Qué demuestra

- Cook-plan validado: `baked_through=RockSlopes_Auto`, `Water` y `Object Layer 1` vivos; la base contiene hierba bajo el agua y la fogata, no el primer frame de esas capas.
- `TileLayerStack` compone la capa de agua y la fogata con el frame actual; el pase de entrada pinta la fila que cruza el borde y el parche solo actualiza celdas cambiadas.
- Prioridad por defecto: si el presupuesto baja, se congela primero `Water` (fondo), luego la fogata (foreground). Congelar no borra lo dibujado: la celda conserva el último frame y las nuevas entradas usan ese frame.
- `kBudgetedAnimations=false` desactiva la degradación para ver el caso completo a ~25 fps; por defecto se intenta preservar 50 fps.

## Pipeline

```bash
node tools/tiled/validate-cook-plan.mjs \
  "../../../Assets/2D/The Fan-tasy Tileset (Free) 1.5.9/Tiled/Tilemaps/Beginning Fields.tmx" \
  tools/tiled/fixtures/beginning-fields.live.cook-plan.json

node tools/tiled/cook-map.mjs \
  "../../../Assets/2D/The Fan-tasy Tileset (Free) 1.5.9/Tiled/Tilemaps/Beginning Fields.tmx" \
  --plan tools/tiled/fixtures/beginning-fields.live.cook-plan.json \
  --out out/assets/tiled/beginning-fields-live \
  --profiles ehb --dither checker --select ehb

bash tools/build/build-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --release
bash tools/run/run-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --sequence-frames 4
```

Presets de cocción:\n\n- `beginning-fields.live-all.cook-plan.json`: todas las capas vivas; `Shadows` usa `shadow_mode: dither` (experimental, sombra en damero).\n\n- `beginning-fields.baked.cook-plan.json`: 50 fps, sin capas vivas.\n- `beginning-fields.fire.cook-plan.json`: solo la fogata viva; agua/flores cocidas.\n- `beginning-fields.live.cook-plan.json`: agua + fogata vivas (el que usa esta demo).\n\n`selected/` contiene `map.bundle`, `tilebank.xlimited.bin` (226 KB), `animations.h`, `anim_sheet.bin` (26,1 KB) y `anim_masks.bin` (4,4 KB): 30 grupos, 136 frames y 165 celdas vivas (`Water` + fogata).

## Medidas

- `A500_release`: **22,7 fps de media** en ventana de 20 s (arranque 12→32; estable ~25–32). No alcanza los 25 estables por el coste de banda/parche.
- La demo arranca en la zona de agua/fogata (pre-scroll) y avanza en Y.
- Secuencia `A500_debug`: agua, flores cocidas y fogata visibles sin huecos evidentes; smoke test visual pendiente de modelo local.
- El coste de la composición completa sigue por encima de un campo; la prioridad se implementa pero el umbral de 50 fps requiere perfilar `compose`/scroll.

## Capas `live` completas (experimental)

El cooker ya puede emitir todos los tiles de una capa como grupos de un frame y omitir la capa entera del bake (`live`). La variante `live-all` se probó: la base queda en `Ground` y las capas superiores se componen en runtime, pero `Shadows` usa opacidad de capa y se veía negra al dibujarse como overlay opaco. Las sombras no deben tratarse como alpha: el cooker debe poder hornearlas como bitplane/índice de sombra o con dithering, o dejar la capa cocida. Para hacer `Flowers` viva con `Road`/`Shadows`/`Water` encima hace falta ese pase de sombra, no solo modos de capa. Por eso la demo entregada usa el sufijo vivo `Water`+`Object Layer 1`.
## Alcance y deuda

- `Flowers` no puede estar viva sin overlays estáticos de las capas superiores (`Road`, `Shadows`, `Water`, objetos): la regla es un prefijo cocido. El validador lo explica y el cook-plan live solo mantiene el sufijo `Water`+objetos.
- El pase `Occluder`/`after_actors` está modelado en `TileLayerStack` pero no hay demo con personaje todavía.
- X+Y simultáneo y el presupuesto fino de bandas quedan pendientes.
