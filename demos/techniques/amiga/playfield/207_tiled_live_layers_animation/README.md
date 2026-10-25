# 207 — Beginning Fields con capas vivas animadas

Demo T5: el bake base de `Beginning Fields` conserva el primer frame de todos los
elementos, y un **animador de celdas** parchea por Blitter las celdas visibles que
cambian de frame. Se animan las flores, el agua y la fogata; las banderolas quedan
estáticas (su TSX no declara animación).

## Qué demuestra

- El cooker host extrae las animaciones de Tiled (grupos, frames, duraciones y
  celdas dispersas) y las emite como banco planar + máscara 1bpp.
- El runtime resuelve `frame(group, tick)` sin dividir por grupo en cada frame
  (acumulador incremental) y parchea solo las celdas de los grupos que cambiaron.
- El pase de parcheo se reparte en 6 fases (`group % 6`) para que un cambio de
  agua (26 grupos) no desborde el `FramePlan` de 128 jobs.
- El recorrido es **Y-only** (la configuración sigue siendo corkscrew XY): 50 fps
  release. Mover X e Y a la vez en esta escena baja a ~25 fps; queda como deuda.

## Pipeline

```bash
# El cook-plan marca todas las capas como baked (base con primer frame) y declara
# en `animations.layers/objects` qué se anima en runtime.
node tools/tiled/cook-map.mjs "C:/Users/dvdjg/Documents/programa/Assets/2D/The Fan-tasy Tileset (Free) 1.5.9/Tiled/Tilemaps/Beginning Fields.tmx" \
  --plan tools/tiled/fixtures/beginning-fields.cook-plan.json \
  --out out/assets/tiled/beginning-fields \
  --profiles 16c,32c,ehb --dither checker --select ehb

bash tools/build/build-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --release
bash tools/run/run-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --config A500_release
```

`selected/` contiene `map.bundle`, `tilebank.xlimited.bin` (225 KB), `animations.h`,
`anim_sheet.bin` (47,6 KB) y `anim_masks.bin` (7,9 KB). El animador tiene 44 grupos,
248 frames y 227 celdas animadas.

## Medidas

- `A500_release`: **49,97 fps**, 141 961 ciclos/frame, 1,0006 campos/frame.
- La cámara arranca en `Y=300` (pre-scroll) para mostrar la fogata, avanza 1 px/frame
  y rebota cada 600 frames.
- `flicker-check` no reporta anomalía confirmada por el modelo; los candidatos
  deterministas son movimiento de cámara + frames de animación.

## Alcance y deuda

- La base se hornea con el **primer frame**; el parche pinta encima el frame actual.
  Los píxeles transparentes de un frame nuevo pueden mostrar el primer frame en vez
  del fondo (ghosting sutil), sobre todo en agua/flores.
- El overlay no se compone en la banda entrante: las celdas nuevas se ven con el
  primer frame hasta el siguiente cambio de grupo. El `TileLayerStack` que compone
  overlays al llenar la banda sigue pendiente.
- X+Y simultáneo a 1 px baja a ~25 fps (2 campos/frame); el recorrido entregado es
  Y-only para mantener 50 fps.
- El agua/flores se ven correctamente y la fogata cambia de forma (verificado con
  visión local); no se ha aislado una medida de animación de agua con cámara fija.
