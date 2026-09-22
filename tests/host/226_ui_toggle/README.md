# HOST-226: GUI G3 — `CheckBox` y `RadioButton` (grupos)

Test host de la fase **G3** de la GUI (`engine/include/eng/ui/widgets.hpp`) sobre `UiContext`.
Ver `ROADMAP_GUI.md` (G3) y `GUI_LIBRARY.md` §10.

## Qué comprueba

1. **CheckBox**: alterna `*value` al soltar dentro; soltar fuera no alterna; deshabilitado no
   alterna.
2. **RadioButton**: al activar uno se desactivan los **hermanos del mismo `group_id`**; otro
   grupo no se toca; el grupo es por `parent` (hermanos).
3. **`measure`**: alto = `check_s`/`radio_s`; ancho = lado + `pad_x` + `text_width(etiqueta)`.

## Salida de referencia

```
OK: GUI G3 (CheckBox + RadioButton/grupos) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/226_ui_toggle
```
