# HOST-311 — Barra de desplazamiento (ScrollBar)

## Qué cubre

`eng/ui/scroll.hpp` (`eng::ui::ScrollBar`): barra de desplazamiento, **vertical u horizontal**,
sobre un `s16*` externo (`min..max`). El **pomo** tiene longitud proporcional a la página visible
(`page`) con un mínimo, y se coloca según el valor; el desplazamiento es **lógico** (fija
`*value`), de modo que el contenedor decide qué pintar.

- Geometría: `track_len()`, `span()`, `thumb_len()`, `thumb_pos()`.
- Interacción: click (posición → valor, centrando el pomo), `nudge`, flechas (1), `Shift`+flecha
  (página), `Home`/`End`; vertical usa `Up`/`Down`, horizontal `Left`/`Right`.

## Invariantes

1. `thumb_len()` ∈ `[kScrollBarMinThumb, track_len]`; `page ≥ span` → pomo a pista completa.
2. `thumb_pos()` mapea `min..max` a `0..(track_len - thumb_len)`.
3. `nudge`/click acotan el valor a `[min, max]`; la orientación ignora las teclas del otro eje.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/ui/311_ui_scrollbar
```

## Relación

- Diseño: `docs/engine/architecture/GUI_LIBRARY.md` §11.
- Plan: `docs/guides/roadmap/ROADMAP_GUI.md` (widgets de scroll).
- Compañero: `ListView` (HOST-312) consume `*value` como índice superior (`top`).
