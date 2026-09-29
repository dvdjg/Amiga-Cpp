# HOST-314 — Layout adaptable (grid, flow/wrap, fit) y texto ajustado

## Qué cubre

Extensiones de `eng/ui/layout.hpp` y `eng/ui/text.hpp`:
- `layout_grid(g, cols, gap_x, gap_y)`: rejilla de `cols` columnas (orden de creación).
- `layout_flow(g, gap_x, gap_y)`: flujo horizontal con **wrap** al llegar al ancho del padre.
- `layout_column_fill(g, gap)`: columna donde cada hijo **expande** su ancho al del padre.
- `layout_fit_children(g, measure, gap, vertical, expand_width)`: ajusta el tamaño de cada hijo a
  su contenido y devuelve el tamaño total (layout **adaptable**).
- `layout_center_column(g, gap)`: columna centrada (diálogos).
- `text_wrap_lines` / `text_wrapped_width` / `draw_text_wrapped` y `Label.wrap`/`wrap_w`: ajuste de
  línea por carácter o por palabra, coherente entre medida (`measure`) y dibujo.

## Invariantes

1. Todos los layouts usan el **orden de creación** de los hijos (`children_creation_order`) y
   capacidad fija (sin heap).
2. `layout_grid` fija la altura de fila por el hijo más alto; `layout_flow` salta de línea cuando
   el siguiente hijo no cabe.
3. `measure` de un `Label` con `wrap` devuelve ancho ajustado y alto = `8 × líneas`.
4. `text_wrap_lines` y `draw_text_wrapped` aplican la misma política de corte.

## Ejecución

```bash
bash tools/run-host-tests.sh tests/host/ui/314_ui_layout_adaptive
```

## Relación

- Diseño: `docs/engine/architecture/GUI_LIBRARY.md` §12; plan: `docs/guides/roadmap/ROADMAP_GUI.md`.
- Base previa: HOST-228 (`layout_stack_v/h`, `anchor`).
