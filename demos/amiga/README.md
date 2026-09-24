# `demos/amiga/` (migración en curso)

Las **técnicas de hardware** de Amiga se movieron a `demos/techniques/amiga/<categoría>/`
(copper, blitter, sprites, playfield, c2p, effects, 3d, audio, input, os, io, debug, setup).

Aquí quedan, pendientes de reubicar como **features** en `demos/features/<feature>/amiga/`
(la lógica ya vive en el engine):

- `060_eng_core_selfcheck` — autocomprobación del core en hardware.
- `123_chess_match` — motor de tablero (ajedrez).
- `124_cards_bench` — motor de naipes.
- `215_gui_widgets`, `300_gui_compositor` — GUI (`eng::ui`).

Ver [`../README.md`](../README.md) y el plan
[`../../docs/guides/roadmap/PLAN_ORGANIZACION_DEMOS.md`](../../docs/guides/roadmap/PLAN_ORGANIZACION_DEMOS.md).
