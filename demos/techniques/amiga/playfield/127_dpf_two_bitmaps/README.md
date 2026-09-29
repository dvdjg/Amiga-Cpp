# 127 - dpf_two_bitmaps

Gate en hardware del **dual playfield de dos bitmaps** compuesto por bandas
(`eng::scene::RasterLayout` + `band_from_dual_view`). Tutorial (`AGENTS.md` §1.12): cada campo se
declara como una **superficie** (`eng::field::PlayfieldHardwareView`), no como punteros de plano.

## Qué muestra

- **PF1** (frontal) y **PF2** (fondo) son dos bitmaps independientes de 3 planos; la composición los
  intercala en los planos pares/impares del hardware con sus módulos y fine scroll
  (`band_from_dual_view`). `Band::bob_target()` da el destino de BOBs de PF1 en este layout
  (interleave de 3 planos).
- PF2 lleva un fondo a rayas; PF1 arranca vacío; los BOB (copia con padding) se dibujan en PF1 con
  `eng::scene::FastBobLayer`.
- Una franja inferior de 0 planos (208..255) cierra la composición (efectos *copper chunky*).
- Los bitmaps y el asset del BOB se generan en runtime (no hay pipeline).

## Cómo se ejecuta

```
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/127_dpf_two_bitmaps --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/127_dpf_two_bitmaps --warp
```
