# Test HOST-034: equivalencia del avance en ráfaga (burst_right)

Respalda `ScrollEngine::burst_right` (`engine/include/eng/field/scroll_engine.hpp`): el avance por
tiles del modo `prefill` calcula la geometría del cruce **una sola vez**, pero debe emitir
exactamente los mismos blits, `save_word` y estado que `tiles*tile_width` pasos de `scroll_right`.

Se compara con un sink de registro (dibujos, costura y estado final) en cuatro escenarios:
alineado, con `stepy != 0` (rama de fillup), 2 tiles de golpe y con inversión de dirección previa
(`restore_saveword`).

```bash
bash tools/run-host-tests.sh tests/host/034_scroll_burst_equiv
```

Contexto: `docs/engine/architecture/FAST_SCROLL.md`.
