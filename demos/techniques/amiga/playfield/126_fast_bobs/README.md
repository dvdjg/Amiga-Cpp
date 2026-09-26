# 126 - fast_bobs

Gate en hardware de la técnica **Fast BOBs** (dual playfield, copia con padding) por la fachada
de escena del engine. Está escrita como **tutorial** (`AGENTS.md` §1.12): el `src/main.cpp`
enseña el camino correcto (bandas + capa de BOBs) sin nombrar registros ni `BlitJob`.

## Qué muestra

- Pantalla compuesta por **bandas** (`eng::scene::RasterLayout`): un **dual playfield 3+3**
  (6 planos, `DBLPF`) de 0..207 y una franja de **0 planos** de 208..255 (DMA de planos apagado,
  para efectos *copper chunky*). La franja se materializa como `ModeSwitchZone`.
- **PF2** (planos impares) lleva el fondo a rayas; **PF1** (planos pares) arranca vacío.
- Los BOB se dibujan en PF1 con `eng::scene::FastBobLayer` (copia opaca con 8 px de padding):
  el propio padding limpia los restos del frame anterior, sin save/restore ni cookie-cut. El
  juego solo mueve actores (`BobActor`).
- El asset del BOB (cuadrado 16×16 con padding) se genera en **runtime** en Chip (planar 3
  planos), sin pipeline de assets.

La ficha de la técnica: [`docs/reference/amiga/techniques/dual-playfield-fastbobs.md`](../../../../docs/reference/amiga/techniques/dual-playfield-fastbobs.md).

## Cómo se ejecuta

```
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/126_fast_bobs --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/126_fast_bobs --warp
```
