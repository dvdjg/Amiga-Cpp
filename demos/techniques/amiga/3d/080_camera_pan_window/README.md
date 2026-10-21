# 080_camera_pan_window — barridos de cámara 3D Full/Fake

Demo de presentación 3D Full/Fake: el recorrido predeterminado barre yaw ±60° (120° de campo), luego pitch ±60° y finalmente combina ambos ejes con una curva Lissajous 1:2 de ±40°. Los Fakes desplazan la ventana en X/Y mientras el Full siguiente se renderiza; el framebuffer sobredimensionado y el clipping general dejan ver superficies y paneles entrando y saliendo del frustum. Técnica documentada en [`camera-pan-fake-frames.md`](../../../../docs/reference/amiga/techniques/camera-pan-fake-frames.md).

- Sala 3D con suelo ocre, techo verde y paredes/fondo azul (colores separados) + techo y pared del fondo distinguibles por color; 4 paneles verticales cian/amarillo/naranja, con el **naranja único** como marcador de frustum. Se rellenan por plano con el Blitter (`PlaneFillBuilder` + `AmigaBackend::fill_polygons_by_plane`).
- Framebuffer 352×256, viewport 320×208 y guardas de 16 px en X/24 filas en Y. **Full**: clipping de cámara general y render del framebuffer; **Fake**: sin render, ventana X por `BPLxPT`/`BPLCON1` y Y por filas completas del puntero. Ambos ejes conservan el contrato de desplazar el último Full dentro de sus márgenes.
- `eng::graphics::PresentController` (HOST-476) aplica pacing y publica Full al inicio de campo. `K080_PITCH_TEST=1` y `K080_LISSAJOUS_TEST=1` fuerzan una fase para validarla por separado. El culling yaw-only se limita a ±65 unidades de paneo (≈14,5°); en pitch y giros amplios se usa el clipping general. La cola asíncrona de blits se aconseja solo con Fast RAM (`async_ok`).
- **Mezcla de colores con damero a nivel de píxel** (`eng::graphics::fill_pattern.hpp`, HOST-474): los dos planos de la sala llevan el **damero 1×1 y su inverso** (`fill_dither_plane_cpu`, 2 filas por plano), y el backend los aplica por fases (`D = A & B`) tras el fill; cada cara queda **fundiendo dos índices base** (paredes/suelo con pares 0/1, 0/2, 1/2) — la paleta gana tonos que no están en ella (oliva = 1⊕2, malva = 1⊕0). Los paneles quedan sólidos (el marcador naranja conserva su color). `-DK080_NO_PATTERN` deja la sala sólida (A/B del coste del paso de patrón, ~+50k al Full).
- HUD en franja de 1 bitplane (líneas 252..299) con versión y telemetría (FRAME/FULL/FAKE, modo+OFF, caras/tamaño, PAN o TILT, K).

## Búsqueda previa

Antes de escribir la demo se comprobó qué existe ya en el engine para no duplicar (regla §1.7):

- **Relleno de la escena**: `eng::graphics::PlaneFillBuilder` + `AmigaBackend::fill_polygons_by_plane` (relleno por plano con contorno EOR + area fill; patrón de `demos/techniques/amiga/blitter/203_polygon_planes`); no se añadió ningún rasterizador nuevo.
- **Política de paneo**: nueva cabecera `eng::graphics/pan_window.hpp` (con test host **HOST-470**): no existía nada equivalente (se buscó `pan`, `fake`, `window` en `engine/`).
- **Convención de scroll fino**: `eng::graphics::fine_scroll_coarse`/`fine_delay`/`fine_scroll_ddfstrt` (`engine/include/eng/graphics/playfield_scroll.hpp`); no se re-derivó la aritmética.
- **Cámara/proyección**: `eng::math3d::load_rotate` (`gfx3d.hpp`) y clipping/proyección de cámara `eng::graphics::{clip_camera_polygon, project_camera_vertex}` (`render3d.hpp`); el recorte 2D final reutiliza `eng::retro::clip_polygon`.
- **Composición de display**: `eng::graphics::composition` (`Scene`, `Scheduler`, `Plan`, `bplcon0_for`) con la lista emitida una vez y parcheada por frame (patrón de `TileScrollScene` en `graphics/drivers/tile_scroll.hpp`).
- **HUD**: `eng::playfield::Surface::draw_text_words` (texto por palabras, demo 120); no se añadió fuente ni bucle por píxel.

No se usan los patrones de bajo nivel vigilados por el gate `prior-art` (`planar_band.hpp`/`blob_batch.hpp`/`add_world_bitmap`): no hay objetos retenidos.

## Documentación de la demo

- `src/INTENT.md` — qué hace y qué demuestra, elementos y evolución temporal.
- `src/VISION.md` — evaluación con el modelo de visión local (prompt canónico).
- `VALIDATION.md` — evidencia: contrato de aislamiento, secuencia, visión, cadencia y deuda.

## Estado (2026-10-21): **BARRIDOS 120° Y LISSAJOUS IMPLEMENTADOS; VALIDACIÓN EN CURSO**

Los barridos puros yaw y pitch pasan 32 frames por paso, con contrato de ventana en X/Y, y la trayectoria Lissajous pasa 15 pares de Fake en comparación bidimensional. Las pruebas congeladas ±60° muestran superficies y paneles entrando/saliendo del frustum; el culling se omite fuera de su alcance yaw validado. La secuencia integrada de 260 muestras cubre yaw, pitch, Lissajous y el retorno al inicio; cambian 258 pares adyacentes y el modelo de visión reconoce los giros aunque confunde algunos valores del HUD. La cadencia base continúa aceptada provisionalmente por el usuario y no se optimiza en esta pasada. Los límites están en `VALIDATION.md`.

## Build / run / analyze

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --sequence-step-frames 16
node ./tools/analyze/verify-080-pan-window.mjs --sequence-dir out/run/080_camera_pan_window/A500_debug/sequence
DEMO_OPT=-O3 DEMO_FLAGS="-fno-tree-vectorize -DK080_PITCH_TEST=1" bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --sequence-step-frames 16
node ./tools/analyze/verify-080-pan-window.mjs --sequence-dir out/run/080_camera_pan_window/A500_debug/sequence --axis y
DEMO_OPT=-O3 DEMO_FLAGS="-fno-tree-vectorize -DK080_LISSAJOUS_TEST=1" bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --debug --clean
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --sequence-step-frames 32
node ./tools/analyze/verify-080-pan-window.mjs --sequence-dir out/run/080_camera_pan_window/A500_debug/sequence --axis xy
node ./tools/analyze/analyze-frame-sequence.sh out/run/080_camera_pan_window/A500_debug/sequence
node ./tools/debug/profile.mjs 080_camera_pan_window A500_debug 4
```

Ablaciones de medición (compiladas, `build.args`): `-DK080_SKIP_HUD`, `-DK080_SKIP_FILL`, `-DK080_SKIP_PATCH`, `-DK080_OFFSET_TEST` (prueba de aislamiento del §1 de VALIDATION).
