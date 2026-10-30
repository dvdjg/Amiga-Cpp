# 209 — Beginning Fields a 3 planos (8 colores)

Variante de bajo coste de la 120: el mapa **Beginning Fields** está pre-dibujado en un bitmap *flat* de 640×640 a **3 planos** y la cámara recorre el mundo con una spline Catmull-Rom cerrada. El scroll no ejecuta ni un blit: mueve `BPLxPT` y `BPLCON1` desde la copperlist. El fuego y las flores se animan por **paleta** (los índices 6/7 del mapa reservados a esas capas se ciclan en el Copper), así que la animación tampoco cuesta DMA de Blitter.

## Assets

- Fuente: `assets/amiga/tiles-reference/Beginning Fields.png` y el compuesto `out/assets/tiled/beginning-fields-live-flowers/composed.png` (no hay TMX en el repo; se usa el composed existente).
- Conversión: `png2amiga --mode lores --depth 3 --dither floyd-steinberg --no-scale --chipset ocs`. Se eligió **Floyd–Steinberg** porque con solo 8 colores fue el que mejor conservó agua/sombras (PSNR 29,45 dB, S2 4,98) frente a Atkinson (29,29 / 4,53) y `opt-checker` (26,16 / −12,47, bandas visibles).
- `tools/tiled/make-3bpp-anim-map.mjs` reserva los índices 6/7 para las celdas vivas de flores/fuego y emite el mundo planar interleaved y la paleta de 8 colores. `tools/tiled/compose-3bpp-live-sprites.mjs` pega el frame 0 de las capas vivas **con su máscara** (anillo de piedra del fuego incluido); sin ese paso el fuego queda como una mancha naranja.

## Técnica

- **3 planos / 8 colores**: `FlatScrollPlayfield` + `XlimitedDisplayComposer`.
- **Cero blits por frame**: la cámara es `s32` (y en la fase 3, 16.8) y el compositor convierte a punteros/fine scroll.
- **Cámara**: Catmull-Rom sobre ocho paradas, cocida a una tabla de **longitud de arco** (`eng/scene/arc_path.hpp`). Paso normal de **6 px/frame constantes**; el modo turbo sube a 16 px/frame. Cada 110 frames hay una parada de 80 frames: la cámara se congela y las paletas de flores y fuego siguen animando.
- **Animación**: paleta por Copper sobre índices reservados (flores crema y fuego ocre, ~6 fps). Sombras y agua quedan **cocidas** en el mapa; con 8 colores, ciclar el azul del agua producía un pulso global perceptible y se descartó.

## Búsqueda previa

Se reutilizan `FlatScrollPlayfield` (`engine/include/eng/field/flat_playfield.hpp`), `XlimitedDisplayComposer` y `HostHud`; no se añade un driver de scroll nuevo. El pipeline de tiles existente (`slice-tiles`/`emit-xlimited-bank`) no admite 3 planos, por lo que se añade el conversor mínimo `make-3bpp-anim-map.mjs` descrito arriba.

## Ejecutar

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/playfield/209_tiled_beginning_fields_3bpp --release
bash tools/run/run-demo.sh demos/techniques/amiga/playfield/209_tiled_beginning_fields_3bpp
```
