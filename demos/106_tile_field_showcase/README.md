# 106_tile_field_showcase

Demo ÚNICA parametrizable del **playfield universal** de campos de tiles:
`TileFieldController` + `DpfDisplayComposer` (de `engine/include/eng/field/`).

En lugar de una demo por feature, esta demo es configurable por parámetros de
compilación (`-D`), de modo que sirve para validar todas las combinaciones sin
duplicar código:

| Parámetro | Valores | Efecto |
|---|---|---|
| `K_TILE_WIDTH` | `16`, `32` | Anchura de tile en px (múltiplo de 16). |
| `K_DUAL` | `1` / `0` | `1` = dual playfield 3+3 (6 planos, transparencia PF1); `0` = single playfield 5 planos (32 colores). |

La abstracción es la misma en todos los casos: **un `TileFieldController` por
playfield** con su propio mapa, framebuffer y cámara. En single playfield solo
hay un controlador (equivalente a un juego con un único PF de 5 bitplanes);
en dual hay dos, unidos por el `DpfDisplayComposer` (la única capa que toca
los registros compartidos BPLxPT/BPLCON1/módulos/BPLCON2/DPF).

## Modos

- **Dual 3+3 (K_DUAL=1, K_TILE_WIDTH=16)**: el modo original de la demo 106.
  Fondo diagonal infinita (PF2) + primer plano Lissajous (PF1, ~50% de tiles
  totalmente transparentes).
- **Tiles anchos (K_TILE_WIDTH=32)**: el tile se copia en UNA pasada del
  Blitter (words_por_fila = tile_width/16). Antes era la demo 107.
- **Single 5 planos (K_DUAL=0)**: un solo playfield de 32 colores con scroll
  infinito, sin DPF. La API no impone dual.

## Compilación

```bash
# Dual 3+3 con tiles de 16px (modo por defecto)
AMIGA_BIN_PATH="..." bash ./tools/build/build-demo.sh demos/106_tile_field_showcase --debug --clean

# Tiles de 32px en dual
AMIGA_BIN_PATH="..." bash ./tools/build/build-demo.sh demos/106_tile_field_showcase --debug --clean \
  -DK_TILE_WIDTH=32

# Single playfield 5 planos con tiles de 16px
AMIGA_BIN_PATH="..." bash ./tools/build/build-demo.sh demos/106_tile_field_showcase --debug --clean \
  -DK_DUAL=0
```

Nota: `build-demo.sh` compila los `*.cpp` de `src/`; las macros se pasan como
argumentos extra. Ajusta según el flujo de build actual del repo.

## Verificación

- **Test unitario del algoritmo** (`tools/analyze/verify-tile-field-fill.mjs`):
  replica `begin` (offset absoluto) y `update` (delta relativo) y verifica que
  el framebuffer de doble página se rellena correctamente (cobertura completa,
  sin solapes, contenido correcto) en múltiples casuísticas: offset en distintas
  zonas, cruce de página hacia adelante y atrás, e inversión de la cámara.
- **Análisis visual**: `bash ./demos/106_tile_field_showcase/analyze-sequence.sh --warp`
  (si existe) o `tools/analyze/analyze-demo.sh`.

## Documentación

- Arquitectura de la API: `docs/architecture/TILE_FIELD_API.md`.
- Notas de sesión y bugs resueltos: `docs/debugging/106_SESION_TILEFIELD.md`.
- Modelo circular 8-way: `docs/architecture/AMIGA_8WAY_SCROLLING.md`.
