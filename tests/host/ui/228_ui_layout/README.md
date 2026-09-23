# HOST-228: GUI G5 — layout y cambio de tema

Test host de la fase **G5**: `eng/ui/layout.hpp` (`layout_stack_v`/`layout_stack_h`, `anchor`) y
el cambio de tema (recolorear). Ver `ROADMAP_GUI.md` (G5) y `GUI_LIBRARY.md` §7/§12.

Los hijos se colocan en **orden de creación** (la lista interna es Z: el último añadido al frente,
así que se invierte).

## Qué comprueba

1. `layout_stack_v`: apila con `gap` en orden de creación; `x` = el del padre.
2. `layout_stack_h`: ídem en horizontal.
3. `anchor`: `BottomRight`/`Center` colocan respecto a los `bounds` del padre.
4. `measure(Button, theme)` = `text_width + 2·pad_x` × `btn_h`.
5. `mark_all_dirty` marca todo el subárbol.
6. **Cambio de tema**: el mismo widget dibujado con otro tema usa su color de texto.

## Salida de referencia

```
OK: GUI G5 (layout + tema) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/ui/228_ui_layout
```
