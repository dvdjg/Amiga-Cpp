# HOST-227: GUI G4 — foco de teclado y `EditBox`

Test host de la fase **G4**: foco de `UiContext` (`Tab`/`Shift+Tab`) y `EditBox`
(`engine/include/eng/ui/editbox.hpp`). Ver `ROADMAP_GUI.md` (G4) y `GUI_LIBRARY.md` §10–§11.

El `EditBox` trabaja sobre un **buffer externo** (`buf`/`cap`/`len`/`caret`) y mantiene el NUL en
`len`; `view` es el scroll horizontal. Las teclas son el contrato de `keys.hpp` (imprimibles =
ASCII/Latin-1; edición/navegación con códigos propios).

## Qué comprueba

1. **Foco**: `Tab` cicla entre widgets `WfAcceptsFocus` en orden de árbol (Z: el último añadido va
   al frente) con vuelta; `Shift+Tab` retrocede; el foco anterior pierde `WfFocused`.
2. `Tab` **no inserta** texto (cambia el foco).
3. **Inserta en medio**, **backspace** y **delete**; `Left`/`Right`/`Home`/`End` mueven el caret.
4. **Capacidad**: no se pasa de `cap-1` caracteres (se reserva el NUL).
5. **Vista horizontal**: `ensure_caret_visible` desplaza `view` cuando el caret sale.

## Salida de referencia

```
OK: GUI G4 (foco + EditBox) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/227_ui_edit
```
