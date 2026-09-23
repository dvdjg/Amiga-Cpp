# HOST-224: GUI G1 — árbol de widgets, dirty y `Panel`/`Label`

Test host de la fase **G1** de la GUI (`engine/include/eng/ui/`): `widget.hpp` (árbol intrusivo
sin heap ni `virtual`), `dirty.hpp` (`DirtyList<Max>`) y `widgets.hpp` (`Panel`/`Label`, despacho
por `switch` y `measure`). Ver `ROADMAP_GUI.md` (G1) y `GUI_LIBRARY.md` §8–§11.

## Qué comprueba

1. **Árbol**: `add_child` enlaza `parent`/`first_child`/`next` y el último añadido queda **al
   frente** (orden Z); `mark_dirty_up` marca el widget y sus ancestros.
2. **Flags**: un `Widget` nace `Visible|Enabled|Dirty`.
3. **`DirtyList`**: fusión por solape, regiones disjuntas se añaden, rect vacío ignorado, `clear`
   y **desborde → repintado total** (`kFullRepaint`).
4. **`measure`**: `Label` mide `text_width × 8`; un contenedor usa sus `bounds`.
5. **Dibujo** (`draw_widget`): `Panel` pinta relleno + bisel del tema; `Label` pinta texto; un
   widget no `Visible` no pinta. Se lee el color por píxel sobre un `ContiguousPlayfield` host.

## Decisiones de G1

- **Despacho**: `switch` exhaustivo sobre `WidgetType` (no tabla de punteros ni `virtual`); el
  compilador avisa (`-Wswitch`) de un tipo nuevo sin rama.
- **Métrica**: texto `text_width(s) × 8` (Font8); botones `text_width + 2*pad_x × theme.btn_h`
  (G2); contenedores sin tamaño intrínseco.

## Estado

API **verificada en host**; falta la demo en hardware (G8, `207_gui_widgets`).

## Salida de referencia

```
OK: GUI G1 (arbol, dirty, medida, Panel/Label) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/224_ui_widgets
```
