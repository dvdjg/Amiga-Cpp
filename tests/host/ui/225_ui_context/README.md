# HOST-225: GUI G2 — `UiContext` (hit-test, foco, despacho) y `Button`

Test host de la fase **G2** de la GUI: `engine/include/eng/ui/context.hpp` (`UiContext`) y
`Button` (`widgets.hpp`). Ver `ROADMAP_GUI.md` (G2) y `GUI_LIBRARY.md` §10.

## Qué comprueba

1. **Hit-test** de delante hacia atrás: en un solape gana el widget añadido al frente; un punto
   fuera de todo devuelve `nullptr`; un widget deshabilitado se salta.
2. **`on_click`** se dispara **una vez** al soltar dentro (MouseDown marca `WfPressed`, MouseUp
   lo limpia y llama al callback).
3. **Soltar fuera** no dispara (el `MouseUp` se entrega al widget enganchado en `MouseDown`).
4. `MouseDown` fuera de todo **no consume** el evento.
5. **Foco** al pulsar un widget que acepta foco.
6. **Dibujo**: el botón normal pinta shine arriba; pulsado, shadow (bisel hundido).

## Salida de referencia

```
OK: GUI G2 (UiContext + hit-test + Button) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/ui/225_ui_context
```
