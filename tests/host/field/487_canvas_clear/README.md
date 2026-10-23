# HOST-487 — limpieza implícita de lienzos y HUD

Blinda la responsabilidad del engine de dejar **deterministas** los lienzos que reserva:
`CanvasPlayfield::begin`, `CanvasPlayfield::clear` y `HudStrip::bind`.

## Casos

- `CanvasPlayfield::begin`: todos los bytes del bitmap Chip empiezan a cero.
- `fill_rect_hw` ensucia el lienzo y `clear()` lo vuelve a dejar negro.
- `HudStrip::bind` limpia una franja externa que llega con basura (`0xaa`).

Memoria Chip sintética; sin WinUAE ni `g_eng_run_status`. Es el test que impide volver a pedir a las demos que limpien el HUD a mano.

```bash
bash tools/run-host-tests.sh tests/host/field/487_canvas_clear
```
