# INTENT - 206_tiled_beginning_fields_baked

## Qué hace

Demuestra el primer nivel de soporte Tiled del engine: el proyecto `Beginning Fields` se compone y cuece en host (`tools/tiled/compose-map.mjs` + `tools/tiled/cook-map.mjs`) y el Amiga consume un **banco interleaved EHB prehecho** y un mapa de índices de 40×40 celdas. La demo mueve la cámara horizontalmente por un mundo toroidal; no parsea TMX/TSX/PNG, no cuantiza y no transforma índices en la CPU del Amiga.

## Qué pretende demostrar

- Que el cooker host resuelve capas, grupos, objectgroups con tile objects, sombras con opacidad y animaciones (primer frame) en una imagen bake coherente.
- Que el formato Amiga se elige a partir de un informe de opciones (16c/32c/EHB) y que EHB es la opción adecuada cuando hay capa de sombras y más de 32 colores; la demo usa el perfil `ehb` seleccionado.
- Que la familia XYLimited conserva 50 fps con un banco de 1183 tiles de 16×16 a 6 planos, mapa 40×40 y scroll horizontal continuo.

## Elementos en pantalla

- **Mapa Beginning Fields** bakeado: terreno, caminos, agua, flores, rocas, edificios, props y sombras de objeto.
- **Cámara horizontal** sobre un mapa toroidal de 640 px de ancho lógico, con cambio de sentido cada 400 frames.
- **HUD de host**: nombre, versión, frame, fps y telemetría `X=cámara`, `T=tiles`, `P=planos`.

## Evolución temporal

1. **Init**: `compose-map`/`cook-map` ya dejaron `selected/` en `out/assets/tiled/beginning-fields/`; la demo carga el banco Chip y el mapa, rellena el anillo y toma el display.
2. **Frames**: la cámara avanza 2 px/frame; el `ScrollEngine` pinta la columna entrante y el compositor presenta. Cada 400 frames invierte el sentido.
3. **Cierre**: no hay final; la escena es un bucle de scroll continuo para validar movimiento y coste.
