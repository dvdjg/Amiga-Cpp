# 102_tile_scroll_dualpf

Dual playfield con scroll independiente por playfield usando la nueva API de
campos de tiles (`TileFieldController` + `DpfDisplayComposer` de
`engine/include/eng/field/`).

La demo usa el modo dual **3+3**: 6 bitplanes en total, con 3 planos en el
primer plano y 3 en el fondo.

- **Fondo** = PF2 (planos pares 2,4,6), 3 bitplanes, paleta `COLOR08..0F`.
  Tiles con glifos hexadecimales y borde, fondo opaco.
- **Primer plano** = PF1 (planos impares 1,3,5), 3 bitplanes, paleta `COLOR00..07`.
  Tiles con tramado del 50% de píxeles (la mitad transparente) más un glifo; el
  color 0 de PF1 es transparente. Además, ~50% de las celdas del primer plano
  usan el tile 63, COMPLETAMENTE transparente, para que el fondo se vea a través
  de tiles enteros.

Cada playfield tiene su propio `TileFieldController` (framebuffer, cámara y
presupuesto de Blitter independientes) y un `DpfDisplayComposer` los une en la
copperlist (BPLxPT, BPLCON1, módulos, BPLCON2, DPF). El scroll es por
sub-píxeles Q16 (seno interpolado con acumulador de resto) para movimiento suave.

Los movimientos (parallax):

- el fondo sigue una ruta por fases (`RouteCamera`): lateral, vertical, diagonal,
  circular y senoidal;
- el primer plano hace una onda de Lissajous propia, en dirección y amplitud
  distintas del fondo (parallax opuesto).

Ambos comparten el mismo mundo FINITO de 64x32 tiles (1024x512 px): la cámara
rebota en los bordes (el controlador limita el scroll al final del mapa). El
`DpfDisplayComposer` es la única capa que toca los registros del chipset.

Comandos:

```bash
bash ./tools/build/build-demo.sh demos/102_tile_scroll_dualpf --debug --clean
bash ./tools/run/run-demo.sh demos/102_tile_scroll_dualpf --sequence-frames 8 --sequence-interval-ms 150
bash ./demos/102_tile_scroll_dualpf/analyze-sequence.sh --warp
```

Esta demo es la migración de la demo original (que usaba `TileScrollScene<Mode>`)
a la nueva API de campos de tiles; la API se documenta en
`docs/architecture/TILE_FIELD_API.md`.
