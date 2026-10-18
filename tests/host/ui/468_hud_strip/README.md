# HOST-468: franja del HUD (`eng::hud::HudStrip`) — ligado, limpieza y materialización de operaciones

Test host de la fase **H1** del HUD: `engine/include/eng/hud/strip.hpp` (`HudStrip` y `apply_hud_ops`). Ver `HUD_SYSTEM.md` §6.3 y `ROADMAP_HUD.md` (H1).

## Qué comprueba

1. **Ligado**: `bind` valida configuración (planos 1..6, ancho/alto > 0) y exige que el bloque Chip cubra `width/8 × planes × height`; un bloque pequeño se rechaza sin tocar el estado.
2. **Limpieza**: `clear()` rellena la banda con el color de fondo del tema en las esquinas.
3. **Operaciones**: `Fill` pinta el rect y no toca fuera; un rect que rebasa el clip pinta lo visible y no cuenta como aplicada; `Frame` pinta el borde de 1 px y deja el interior; `Text` pinta glifos (`Font8`) y, en celda alineada a 16 px, usa la ruta de palabra completa (`draw_text_words`: sustituye el fondo de la celda sin tocar fuera); las operaciones vacías y `Blit`/`Sprite`/`Palette` (H4/H6) no cuentan.
4. **Coordenadas de banda**: el lienzo es `CanvasPlayfield` interleaved con la `Surface` recortada a `(0,0,width,height)`.

## Salida de referencia

```
HOST-468 hud_strip: OK
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/ui/468_hud_strip
```
