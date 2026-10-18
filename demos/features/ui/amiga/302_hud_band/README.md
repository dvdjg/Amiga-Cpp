# Demo 302 — `hud_band`: HUD de juego en franja propia (`eng::hud`)

Demo de la fase **H1** del [`ROADMAP_HUD.md`](../../../../../docs/guides/roadmap/ROADMAP_HUD.md): un juego mínimo (actor que rebota) con un **HUD de acción** en una **franja inferior de 48 líneas a 2 planos con paleta propia** (blanco/amarillo/rojo), compuesta con `ModeSwitchZone` en la línea de display 208. El HUD usa el modelo `eng::hud` (`Counter` 6 dígitos, `Meter` lineal de energía, `Meter` de 4 *pips* de vidas) y el presentador `HudStrip` sobre un `CanvasPlayfield` (Chip) con `Surface`; el juego solo declara la región y actualiza valores.

## Qué enseña

- **Región declarativa**: `HudRegionSpec` (`Band`, `top = 208`, `height = 48`, `planes = 2`, `own_palette`) con presupuesto de operaciones; el juego no escribe registros ni conoce el chipset.
- **Composición por la fachada**: el campo de juego es la escena del `App` (`band_from_view` sobre su `CanvasPlayfield` interleaved) y la franja se añade como segunda banda (`band_of_planes` + paleta) con `app.present_layout`; el `WAIT`/`BPLxPT`/paleta los emite el compositor.
- **Dirty real**: `set_counter`/`set_meter` marcan *dirty* solo si el valor visible cambia; el repintado materializa `Fill`/`Frame`/`Text`/`Digits` sobre la banda **por deltas** (la barra solo repinta el tramo que cambia, los pips el que cambia de estado y los dígitos usan `draw_text_words` al estar la celda alineada a 16 px).
- **HUD de depuración** (AGENTS §0): línea `HUD BAND vX.Y.BBBB F NNNNN` de **longitud fija** por palabras (`Surface::draw_text_words`), repintada una vez por segundo sin borrar la fila.
- **Presupuesto**: el actor se borra/pinta con `fill_box` (Blitter, `FillRect` por plano) y el estado del juego vive en BSS; el frame entra en 1 campo (ver `VALIDATION.md`).

## Búsqueda previa

Antes de escribir la demo se comprobó qué existe ya en el engine para no duplicar:

- **Presentación de HUD**: `eng::hud` (`Hud`, `HudStrip`, `HudRegionSpec`, `HudTheme`) en `engine/include/eng/hud/`; la franja se pinta con `eng::playfield::Surface` sobre `eng::playfield::CanvasPlayfield` (no hay `BitmapView` propio ni bucle por píxel).
- **Composición de bandas**: `eng::scene::Band`/`band_of_planes`/`band_from_view`/`RasterLayout` + `App::present_layout` (la vía de la demo 131), en vez de bajar a `copper::Scheduler`/`ModeSwitchZone` a mano.
- **Fachada de juego**: `eng::GameDisplay` + `eng::amiga::run_demo` + `App::screen()` (demo 122); la línea de depuración reutiliza la técnica de `eng::debug::draw_hud` (texto por palabras con `Surface::draw_text_words`), con formato de longitud fija propio (el helper no rellena el número de frames).
- **Texto**: `Surface::draw_text`/`draw_text_words` y `Font8`; no se añadió rasterizador de fuentes.

## Ejecutar

```bash
bash ./tools/build/build-demo.sh demos/features/ui/amiga/302_hud_band --debug
bash ./tools/run/run-demo.sh demos/features/ui/amiga/302_hud_band --keep-running
bash ./tools/analyze/analyze-demo.sh demos/features/ui/amiga/302_hud_band
```

Verificación y medidas: [`VALIDATION.md`](VALIDATION.md) · Intención: [`src/INTENT.md`](src/INTENT.md) · Visión (prompt canónico): [`src/VISION.md`](src/VISION.md).

## Notas de implementación

- El estado del juego (`Hud<>` son ~7 KB con las capacidades por defecto) vive en **BSS**, no en la pila: un local en `main` desbordaba la pila del arranque Amiga (16 KB) y corrompía el modelo.
- `Band::top` es **línea de raster** (`WAIT` del Copper sobre VPOS): la línea de display 208 empieza en `0x2c + 208` (AHRM cap. 3; patrón de la demo 115).
- El repintado **por deltas** es la clave del presupuesto: pintar el HUD completo por CPU costaba ~460k ciclos/frame (11,9 fps); con deltas, `draw_text_words`, actor por Blitter y versión de longitud fija entra en 1 campo (ver `VALIDATION.md`).
- La franja se escribe **en el mismo frame** (tras el VBlank) con margen de sobra hasta la línea 208; la decisión de doble buffer de banda queda para cuando haya contenido que lo requiera (ver `HUD_SYSTEM.md` §6.3).
