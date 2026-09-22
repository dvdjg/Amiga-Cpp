# HOST-229: GUI G6 — ventanas (Window/Popup/Toast/Dialog)

Test host de la fase **G6**: `eng/ui/window.hpp` y la política de `UiContext` sobre ventanas. Ver
`ROADMAP_GUI.md` (G6) y `GUI_LIBRARY.md` §13.

## Qué comprueba

1. **Z / raise**: el hit-test devuelve el de delante; `raise` lo sube al frente y reenlaza la lista.
2. **Modal** (`Dialog`, `WfModal`): filtra el hit-test y el despacho (el escritorio de detrás no
   recibe); dentro del modal sí.
3. **`Esc`** cierra el diálogo/popup superior (oculta la ventana).
4. **Toast** (`WfNoInput`): no capta input y expira por `Tick` (TTL en frames).
5. **Popup**: un click fuera lo cierra y se consume.

## Salida de referencia

```
OK: GUI G6 (ventanas + modalidad) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/229_ui_windows
```
