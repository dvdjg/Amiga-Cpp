# HOST-223: GUI G0 — `UiPainter`, `UiTheme` y texto

Test host de la fase **G0** de la librería GUI (`engine/include/eng/ui/`): `theme.hpp`
(`UiTheme` + presets), `painter.hpp` (`UiPainter`) y `text.hpp` (`text_width`,
`draw_text_clipped`), sobre `field::Surface`. Ver `ROADMAP_GUI.md` (G0) y
`GUI_LIBRARY.md` §5–§7.

Pinta sobre un `ContiguousPlayfield` enlazado a **memoria host** (sin Chip) y **lee el color de
cada píxel** del mapeo planar, así que comprueba *dónde* cae el chrome (no solo cuántos bits).

## Qué comprueba

1. **Rect**: `eng::ui::Rect` es `eng::Box`; `clip()` coincide con la `Surface`.
2. **`fill`**: rellena dentro y no se sale del rect.
3. **`frame`**: perímetro de 1 px con el color pedido; interior intacto.
4. **`bevel_out`/`bevel_in`**: shine/shadow en los lados correctos (arriba/izquierda vs
   abajo/derecha), intercambiados entre relieve y hundido.
5. **`panel`**: rellena con `fill` y aplica el marco del tema (`Raised` → bevel_out; `Flat` →
   sin bisel).
6. **`button_face`**: normal = `fill`+bevel_out; pulsado = `fill_active`+bevel_in.
7. **`glyph`**: glifo 1-bit MSB-primero (columna 0 = bit `w-1`).
8. **`text_width`**: 0/24 px/acento UTF-8 (mide **code points**, no bytes).
9. **`draw_text_clipped`**: dibuja los glifos que caben y **no parte** el que no cabe (comparado
   con el texto sin recorte).
10. **Presets**: `kThemeWb13` con relieve, `kThemeFlat` sin bisel.

## Estado

API **verificada en host**; falta su **demo en hardware** (llega en G8, `207_gui_widgets`), así que
no se documenta como validada en Amiga.

## Salida de referencia

```
OK: GUI G0 (UiPainter/UiTheme/texto) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/ui/223_ui_painter
```
