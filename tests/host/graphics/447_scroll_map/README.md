# HOST-447 — scroll_map

Test host del **mapa grande con scroll** sobre `eng::playfield::TileLayerMap`: el puente
entre el mundo de biomas (`terrain_biome.hpp`) y las capas de scroll.

## Qué cubre

- Genera un mundo grande (**40×28** celdas de 16×16) con `generate_biome_world` y comprueba
  que el set **memoiza por máscara** (las piezas distintas crecen muy por debajo del número
  de celdas) y que el mundo está conectado.
- Envuelve el layout como `TileLayerMap` **sin conversión** (`cells`/`width`/`height`) con
  **wrap toroidal en X** y verifica la semántica de cámara de scroll:
  - ventanas de 20×14 en offsets crecientes (3 barridos completos, incluidos los cruces de
    la costura): `tile_at` == layout indexado a mano con módulo, e índices válidos;
  - offsets **negativos** (scroll hacia atrás): `-1 ≡ width-1`;
  - **sin wrap en Y**: fuera del mundo → `edge_tile`; la última fila sigue dentro;
  - ancla concreta: una celda de agua reaparece idéntica un periodo de wrap después.

## Nota: integración con el driver X-Limited

Este test cubre la **semántica lógica** (qué tile toca en cada celda de la ventana). La
integración con el **driver de scroll X-Limited** (banco de tiles por bloques de 32 px,
copper por zonas) es el paso siguiente de la hoja de ruta y va **aparte**: el área
`field/xlimited_*` está siendo refactorizada (modelo tipado del Copper) y un cambio de
demo ahí colisiona con ese trabajo en curso. Cuando aterrice, la demo debe: construir el
banco desde `BiomeTileSet`, envolver el layout como `TileLayerMap` (este test) y dejar que
la cámara de `XLimitedScene` consulte por `tile_at`.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/447_scroll_map
```
