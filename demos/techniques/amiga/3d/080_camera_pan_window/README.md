# 080_camera_pan_window — fotogramas intermedios por paneo de cámara (F1)

Demo de la fase F1 del [`ROADMAP_3D_PRESENTACION.md`](../../../../docs/guides/roadmap/ROADMAP_3D_PRESENTACION.md): pasadas completas de escena 3D poligonal con **base ≥12,5 Hz** y display interpolado **≥25 Hz**, moviendo solo la **ventana de display** dentro de un framebuffer horizontal más ancho que el viewport para interpolar varios fotogramas por pasada. Técnica documentada en [`camera-pan-fake-frames.md`](../../../../docs/reference/amiga/techniques/camera-pan-fake-frames.md).

- Sala 3D (tubo: suelo, techo, paredes, pared del fondo) + 4 paneles verticales de color (uno **naranja único**, marcador de movimiento), rellenos con el relleno por plano del Blitter (`PlaneFillBuilder` + `AmigaBackend::fill_polygons_by_plane`).
- Framebuffer 352×208 (viewport 320 con margen de 16 px por lado). **Full**: render del FB entero y ventana centrada (S=16). **Fake**: sin render; solo se parchean `BPLxPT` (coarse) y `BPLCON1` (fino) con la convención de `playfield_scroll.hpp` para desplazar la ventana: el Fake es **exactamente** el Full anterior desplazado (contrato de píxel).
- Política `eng::graphics::pan_window.hpp` (HOST-470): margen 15 px; fase lenta (1 px/frame) = fakes con el offset exacto y fase rápida (16 px/frame) = Full cada frame. El **número de fakes por pasada es adaptativo** (`eng::graphics::render_pacing.hpp`, HOST-471): EMA del coste medido del Full en campos PAL y `affordable_fakes` con tope K≤3.
- HUD en franja de 1 bitplane (líneas 252..299) con versión y telemetría (FRAME/FULL/FAKE, modo+OFF, caras/tamaño, PAN, K).

## Búsqueda previa

Antes de escribir la demo se comprobó qué existe ya en el engine para no duplicar (regla §1.7):

- **Relleno de la escena**: `eng::graphics::PlaneFillBuilder` + `AmigaBackend::fill_polygons_by_plane` (relleno por plano con contorno EOR + area fill; patrón de `demos/techniques/amiga/blitter/203_polygon_planes`); no se añadió ningún rasterizador nuevo.
- **Política de paneo**: nueva cabecera `eng::graphics/pan_window.hpp` (con test host **HOST-470**): no existía nada equivalente (se buscó `pan`, `fake`, `window` en `engine/`).
- **Convención de scroll fino**: `eng::graphics::fine_scroll_coarse`/`fine_delay`/`fine_scroll_ddfstrt` (`engine/include/eng/graphics/playfield_scroll.hpp`); no se re-derivó la aritmética.
- **Cámara/proyección**: `eng::graphics::project_perspective` + `eng::math3d::load_rotate` (`gfx3d.hpp`) y recorte 2D `eng::retro::clip_polygon` (lib2d); no se duplicó geometría.
- **Composición de display**: `eng::graphics::composition` (`Scene`, `Scheduler`, `Plan`, `bplcon0_for`) con la lista emitida una vez y parcheada por frame (patrón de `TileScrollScene` en `graphics/drivers/tile_scroll.hpp`).
- **HUD**: `eng::playfield::Surface::draw_text_words` (texto por palabras, demo 120); no se añadió fuente ni bucle por píxel.

No se usan los patrones de bajo nivel vigilados por el gate `prior-art` (`planar_band.hpp`/`blob_batch.hpp`/`add_world_bitmap`): no hay objetos retenidos.

## Documentación de la demo

- `src/INTENT.md` — qué hace y qué demuestra, elementos y evolución temporal.
- `src/VISION.md` — evaluación con el modelo de visión local (prompt canónico).
- `VALIDATION.md` — evidencia: contrato de aislamiento, secuencia, visión, cadencia y deuda.

## Estado (2026-10-18): **ABIERTA / NO VERIFICADA**

El contrato de píxel del paneo (mecanismo exacto) y la política Full/Fake adaptativa están probados; el **display cumple su suelo (≥25 Hz: medido ~28 fps, 1,79–1,80 campos/frame con K=2)**, pero la **base (Full ≥12,5 Hz) aún no**: la pasada completa mide ~3,4 campos (426k–551k ciclos) y con la regla de pacing sale K=2, dejando la base en ~9 Hz. Cerrar F1 exige bajar el Full a ≤3,0 campos (K=1) con la campaña de optimización (deuda DT-032…DT-034 en VALIDATION §7); la secuencia de contrato con la política adaptativa queda pendiente de recapturar (DT-033, captura por paso bloqueada).

## Build / run / analyze

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --sequence-step-frames 16
# La captura por paso esta bloqueada por DT-033 (GDB socket timeout); alternativa que funciona:
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --sequence-frames 32 --sequence-interval-ms 60
node ./tools/analyze/verify-080-pan-window.mjs --sequence-dir out/run/080_camera_pan_window/A500_debug/sequence
node ./tools/debug/profile.mjs 080_camera_pan_window A500_debug 4
```

Ablaciones de medición (compiladas, `build.args`): `-DK080_SKIP_HUD`, `-DK080_SKIP_FILL`, `-DK080_SKIP_PATCH`, `-DK080_OFFSET_TEST` (prueba de aislamiento del §1 de VALIDATION).
