# HOST-312 — Lista con selección y desplazamiento (ListView)

## Qué cubre

`eng/ui/list.hpp` (`eng::ui::ListView`): lista de cadenas propias con **selección** (índice externo
`s16*`, `-1` = ninguno) y **desplazamiento vertical lógico** (`top`): solo se pintan las filas
visibles. Con foco: flechas (1), `Shift`+flecha (página), `Home`/`End`; el click selecciona la fila
bajo el cursor.

- Medida: `visible_rows()` = `bounds.h / item_h`; `clamp_top()` acota `top`.
- Selección: `select(i)` + `ensure_visible()` (desplaza para dejar visible la fila elegida).

## Invariantes

1. `top` ∈ `[0, max(0, count - visible_rows)]`.
2. `select(i)` se ignora fuera de `[0, count)`; tras seleccionar, la fila queda visible.
3. El click selecciona `top + (y - bounds.y - 1) / item_h` (la fila bajo el cursor).

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/ui/312_ui_listview
```

## Relación

- Diseño: `docs/engine/architecture/GUI_LIBRARY.md` §11.
- Plan: `docs/guides/roadmap/ROADMAP_GUI.md` (widgets de scroll).
- El `top` puede enlazarse con un `ScrollBar` (HOST-311) que fije el índice superior.
