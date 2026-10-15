# HOST-443 — proc_golden

Test host **golden** de la generación procedural (`eng/graphics/proc/`): congela el render
(paleta, texturas, piezas y colocador) con `MiniFloat16` (aritmética entera, portable byte a
byte) mediante hashes FNV-1a.

## Qué cubre

- **Paleta** del atlas (16 colores RGB444 con jitter por seed).
- **Texturas**: las 22×3 muestras de material del `TextureAtlas`.
- **Piezas**: las 42 del catálogo de terreno del `TerrainTileSet`.
- **Colocador**: el mapa 10×7 de `place_world` con **dos semillas** (ambos conectados por
  `layout_is_connected`).

Si un refactor cambia el render —colores, ruido, piezas o colocación— el hash cambia y el
test falla: la decisión de aceptar el cambio visual es explícita (actualizar `kGolden*` con
los valores que el propio test imprime). Es también la red que garantiza que optimizaciones
como la rejilla de nodos (`HOST-444`) y los atajos sin ruido de las primitivas **no** cambian
un solo píxel.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/graphics/443_proc_golden
```
