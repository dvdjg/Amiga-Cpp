# 080_camera_pan_window — fotogramas intermedios por paneo de cámara (F1)

Demo de presentación 3D Full/Fake: una pasada completa renderiza la sala y el display interpola movimientos pequeños desplazando una ventana dentro de un framebuffer sobredimensionado. El modo predeterminado prueba yaw/Y con paneo horizontal (F1); `K080_PITCH_TEST=1` prueba pitch/X con paneo vertical por filas completas (F3 experimental). Técnica documentada en [`camera-pan-fake-frames.md`](../../../../docs/reference/amiga/techniques/camera-pan-fake-frames.md).

- Sala 3D (tubo: suelo, techo, paredes, pared del fondo) + 4 paneles verticales de color (uno **naranja único**, marcador de movimiento), rellenos con el relleno por plano del Blitter (`PlaneFillBuilder` + `AmigaBackend::fill_polygons_by_plane`).
- Framebuffer 352×224 con viewport 320×208, márgenes de 16 px en X y ocho filas en Y. **Full**: render del FB entero y ventana centrada (S=16, T=8). **Fake**: sin render; se parchean `BPLxPT` y `BPLCON1` para X, y el puntero por filas completas para Y; el Fake representa el Full anterior desplazado (contrato de píxel).
- La política 2D `eng::graphics::pan_window.hpp` (HOST-470) comprueba ambos márgenes; `PresentController` (HOST-476) devuelve offsets X/Y, aplica pacing medido y publica el Full al inicio de campo. El modo predeterminado panea yaw/eje Y en X; el build diagnóstico `K080_PITCH_TEST=1` panea pitch/eje X en Y usando filas OCS completas y clipping de cámara general. El camino asíncrono por cola de blits se aconseja solo con Fast RAM (`async_ok`).
- **Mezcla de colores con damero a nivel de píxel** (`eng::graphics::fill_pattern.hpp`, HOST-474): los dos planos de la sala llevan el **damero 1×1 y su inverso** (`fill_dither_plane_cpu`, 2 filas por plano), y el backend los aplica por fases (`D = A & B`) tras el fill; cada cara queda **fundiendo dos índices base** (paredes/suelo con pares 0/1, 0/2, 1/2) — la paleta gana tonos que no están en ella (oliva = 1⊕2, malva = 1⊕0). Los paneles quedan sólidos (el marcador naranja conserva su color). `-DK080_NO_PATTERN` deja la sala sólida (A/B del coste del paso de patrón, ~+50k al Full).
- HUD en franja de 1 bitplane (líneas 252..299) con versión y telemetría (FRAME/FULL/FAKE, modo+OFF, caras/tamaño, PAN o TILT, K).

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

## Estado (2026-10-20): **YAW VERIFICADO EN EL ALCANCE MEDIDO; PITCH EXPERIMENTAL**

El contrato Full/Fake de yaw pasó una nueva secuencia de 32 frames tras los cambios del controlador, y el culling no cambia los píxeles en poses congeladas de pan +13/+65. El build de prueba `K080_PITCH_TEST=1` activa paneo vertical/pitch; su contrato paso-a-paso pasa 32 frames con `--axis y`, y una secuencia visual de intervalo cambia en 23/23 pares. Ollama dio falsos negativos e interpretaciones parciales incluso al comparar extremos recortados; los diffs objetivos y la inspección de frames confirman el desplazamiento, con alcance y costuras documentados en `VALIDATION.md`. La cadencia base de F1 sigue por debajo de 12,5 Hz; el usuario acepta provisionalmente los FPS actuales para continuar sin relajar el umbral del roadmap.

## Build / run / analyze

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --sequence-step-frames 16
node ./tools/analyze/verify-080-pan-window.mjs --sequence-dir out/run/080_camera_pan_window/A500_debug/sequence
DEMO_OPT=-O3 DEMO_FLAGS="-fno-tree-vectorize -DK080_PITCH_TEST=1" bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --sequence-step-frames 16
node ./tools/analyze/verify-080-pan-window.mjs --sequence-dir out/run/080_camera_pan_window/A500_debug/sequence --axis y
node ./tools/analyze/analyze-frame-sequence.sh out/run/080_camera_pan_window/A500_debug/sequence
node ./tools/debug/profile.mjs 080_camera_pan_window A500_debug 4
```

Ablaciones de medición (compiladas, `build.args`): `-DK080_SKIP_HUD`, `-DK080_SKIP_FILL`, `-DK080_SKIP_PATCH`, `-DK080_OFFSET_TEST` (prueba de aislamiento del §1 de VALIDATION).
