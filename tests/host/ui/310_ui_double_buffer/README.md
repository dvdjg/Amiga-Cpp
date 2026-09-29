# HOST-310 — Pantalla de doble buffer (flicker-free)

## Qué cubre

`eng/ui/double_buffer.hpp` (`eng::ui::DoubleBufferScreen`): dos buffers de pantalla y un *publish*
(flip) para que el compositor **no parpadee**. El compositor compone en el buffer **trasero**
mientras el display sigue leyendo el **delantero**; `flip()` intercambia y llama al *publisher*
(parcheo de `BPLxPT`/`COP1LC` en VBlank en Amiga).

- `back()` / `front()` / `flip()` / `set_publisher()`.
- `present(Compositor&, FramePlan&)` / `present(Compositor&)`: compone en el trasero y publica.

## Invariantes

1. Componer en `back()` **no** cambia `front()` (el display no ve una composición a medias).
2. `flip()` intercambia buffers y llama al publisher **una vez**.
3. `present()` equivale a componer en `back()` + `flip()`.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/ui/310_ui_double_buffer
```

## Relación

- Diseño: `docs/engine/architecture/GUI_LIBRARY.md` §14.
- Plan: `docs/guides/roadmap/ROADMAP_GUI.md` (paso de doble buffer).
- El *publisher* se conecta en la demo al display (p. ej. `scene.commit()`/`copper::DoubleBuffer`).
