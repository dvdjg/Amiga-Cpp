# Demo 202 — DPF 3+3 (dual playfield) con mapa real a 8 colores + plaquettes transparentes

## Qué hace

Una escena de **dual playfield (DPF 3+3)** sobre el corkscrew X-Limited del engine, con dos capas y un recorrido que visualiza el mapa:

- **BG (PF2, planos HW 2,4,6)**: el mundo real de la 201 (Beginning Fields, 40×40 tiles) cuantizado a 8 colores (3 planos, paleta `mediancut` + dither `atkinson`), toroidal.
- **FG (PF1, planos HW 1,3,5)**: plaquettes transparentes (7 colores + índice 0).
- **Recorrido**: fases lineales a 1 px/frame, luego Lissajous; el paso máximo se limita a 1 px/frame en la variante entregada.
- **Mapas toroidales** y anillo de 288 líneas con ventana visible de 208.
- Scroll fino por hardware (`BPLCON1` + `BPLxPT` vía Copper); el Blitter sólo pinta la columna/fila entrante en cada cruce de 16 px.

## Configuración entregada (50 Hz)

- `kShareY = true`: los dos campos comparten la Y del corkscrew y un único split; se elimina el campo lineal/mirror y sus blits duplicados. Es la reducción necesaria para caber en un campo PAL.
- `max_step = 1`: menos sub-pasos de scroll por frame.
- HUD de host a 1/8 de frecuencia de refresco; sigue visible y no consume Copper.
- Se eliminó el bloque de instrumentación `DEBUG` de `XLimitedPlayfield::add_draw` y se cachea el bloque de mapa por copia/espejo.
- Resultado: **50,16 fps emulados / 141.435 ciclos/frame** en release y **49,92 fps** en debug. Detalle en `VALIDATION.md`.

La ruta con Y independiente (`kShareY=false`) queda como variante de calidad, pero no cabe en 50 Hz con el DPF completo; se documenta como deuda.

## Ejecución

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/playfield/202_xlimited_dpf --release
bash tools/run/run-demo.sh demos/techniques/amiga/playfield/202_xlimited_dpf --config A500_release
```
