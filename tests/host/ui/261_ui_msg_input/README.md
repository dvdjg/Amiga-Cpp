# HOST-261: GUI — entrada por mensajes (`os::Msg` → `UiContext`)

Test host de la integración **`eng::ui` ↔ `eng::os`**: la GUI consume la entrada **por mensajes**
del mini-SO (no por sondeo), según `MINI_OS_INPUT.md` y `GUI_LIBRARY.md` §15.

## Qué comprueba

1. **`keymap`** (`keymap.hpp`): rawkey Amiga → tecla lógica (`keys.hpp`), con Shift (mayúsculas y
   símbolos) y teclas de edición/navegación (Backspace, Tab, Return, Esc, flechas).
2. **`dispatch_msg`** (`msg_adapter.hpp`): un `os::Msg` de `KeyDown` se traduce (`ui_bridge`) y
   despacha al widget con foco; un `EditBox` recibe `asA` y borra con Backspace.
3. Un mensaje **no de entrada** (VBlank) no se consume.
4. `MouseButton` → `MouseDown`/`MouseUp` despachado a un `Button` dispara `on_click`.

## Salida de referencia

```
OK: GUI entrada por mensajes (keymap + dispatch_msg) validada.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/261_ui_msg_input
```
