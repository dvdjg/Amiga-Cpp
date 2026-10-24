# Validacion de la demo 091 - Scene3D retenida

Estado: **FUNCIONAL, VISION OK, CADENCIA VERIFICADA en alambre directo**. La escena jerarquica, la rotacion, los materiales y el HUD funcionan. La ruta directa `retro::wire_lines` mide 27,11 fps debug / 30,57 fps release; la ruta rellena (8,39 fps) y la silueta (12,64 fps) siguen abiertas.

## 1. Alcance y evidencia

| Elemento | Evidencia | Veredicto |
|---|---|---|
| `Scene` retenida, handles generacionales, jerarquia y rotacion | HOST-488 (`tests/host/graphics/488_scene3d_api`) | **OK en host** |
| `ModelTable` + `Model3DAssetView` + `cook-model3d` | HOST-488 + `node dist/tools/assets/cook-model3d.js --self-test` | **OK en host** |
| `FaceStyle` + patron CPU + `CameraView` + `render_wire`/`clip_camera_segment` | HOST-488 | **OK en host** |
| Demo en WinUAE: dos cubos de alambre, jerarquia y rotacion | Secuencia de 4 frames + [`src/VISION.md`](src/VISION.md) | **OK en vision** |
| HUD de host (nombre, version, frames, fps, telemetria) | Capturas de la secuencia | **OK** |
| Cadencia directa (1 plano, 18 aristas, debug) | `measure-fps.mjs` | **OK: 27,11 fps / 1,84 campos (261.644 ciclos)** |
| Cadencia directa (release A500) | `measure-fps.mjs` | **OK: 30,57 fps / 1,64 campos (232.038 ciclos)** |
| Continuidad/flicker de secuencia | `node tools/analyze/verify-3d-sequence.mjs out/run/091_scene3d_nodes/A500_debug/sequence --min-frames 3 --require-motion` | **OK: 3 frames, movimiento, sin saltos de area** |
| Variante rellena (`K091_WIRE=0`, 2 cubos, doble buffer) | 3 frames + validacion de vision; `measure-fps.mjs` | **OK en vision; 8,39 fps / 5,96 campos; cadencia abierta** |

## 2. Comandos

```bash
bash ./tools/build/build-demo.sh demos/techniques/amiga/3d/091_scene3d_nodes --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/3d/091_scene3d_nodes --settle-ms 1500 --sequence-frames 4 --sequence-interval-ms 400
node ./tools/debug/measure-fps.mjs 091_scene3d_nodes A500_debug --json
node ./tools/analyze/vision-final.mjs 091_scene3d_nodes out/run/091_scene3d_nodes/A500_debug/sequence --demo-dir demos/techniques/amiga/3d/091_scene3d_nodes
```

La variante rellena se compila con `K091_WIRE=0` en `DEMO_FLAGS`.

## 3. Medidas (A500, debug, PAL)

| Ruta | Cubos | Buffer | Update/frame | Campos/frame | fps |
|---|---:|---|---:|---:|---:|
| **Relleno por cara (`K091_WIRE=0`, actual)** | **2** | **2** | **845.925 ciclos** | **5,96** | **8,39** |
| **Silueta convexa (`K091_WIRE=0 K091_SILHOUETTE=1`, P8 A/B)** | **2** | **2** | **561.192 ciclos** | **3,96** | **12,64** |
| Relleno por cara (historica, 3 cubos) | 3 | 1 | 1.128.714 ciclos | 7,96 | 6,28 |
| Alambre CPU (`draw_line` sin plan) | 3 | 1 | 2.392.418 ciclos | 16,86 | 2,97 |
| Alambre Blitter Copy | 2 | 1 | 964.533 ciclos | 6,80 | 7,35 |
| **Alambre Blitter EOR, 1 plano, 18 aristas visibles (debug)** | **2** | **2** | **321.398 ciclos** | **2,26** | **22,07** |
| **Alambre directo `wire_lines` (release A500)** | **2** | **2** | **232.038 ciclos** | **1,64** | **30,57** |
| Alambre por `FramePlan` 1 plano (release) | 2 | 2 | 294.897 ciclos | 2,08 | 24,06 |

| Cadencia directa (1 plano, 18 aristas, debug) | `measure-fps.mjs` | **OK: 27,11 fps / 1,84 campos (261.644 ciclos)** |
| Cadencia directa (release A500) | `measure-fps.mjs` | **OK: 30,57 fps / 1,64 campos (232.038 ciclos)** |

## 4. Vision (Ollama, prompt canonico)

`src/VISION.md` (generado con `vision-final.mjs`, 4 frames en orden temporal) describe dos cubos tridimensionales de **lineas blancas discontinuas** sobre fondo negro, el superior mas grande e inclinado y el inferior mas pequeno y en orbita; **no observa halos, bandas rectangulares, restos de posiciones anteriores ni pixeles cortados**. El modelo lee el HUD (`091 SCENE3D NODES`, frames y fps) y confunde algun digito del segundo renglon; es una limitacion de OCR, no del render. La linea `ONEDOT` del modo linea del Blitter produce el aspecto punteado de las aristas diagonales; es la tecnica documentada, no un defecto.

La secuencia de la **variante rellena** se valido aparte con `vision-final.mjs` en un directorio temporal (3 frames): describe dos cubos solidos, el cian mayor y el hijo pequeno, con **contornos limpios, sin huecos ni muescas**, y confirma que el fondo negro queda visible alrededor. Es la evidencia visual del arreglo del DDA de `convex_spans`.

## 5. Limites y siguientes gates

- **Cadencia**: alambre directo 30,57 fps release (27,11 debug); ruta por plan 24,06; relleno por cara 8,39 y silueta 12,64. El alambre directo cumple 25 fps; las rutas de relleno siguen abiertas.
- **Flicker**: `verify-3d-sequence.mjs` confirma movimiento y ausencia de saltos de area entre frames; no sustituye una captura a 50 Hz de todas las poses, pero es la primera regresion automatica.
- **Poses**: la secuencia cubre cuatro poses; no barre todos los angulos ni el cruce por near del hijo.
- **Regresion corregida**: `convex_spans` avanzaba como maximo 1 px por fila en aristas con `|dx| > dy` (DDA con `if` en vez de `while`), lo que dejaba muescas en caras y cubos rellenos. El arreglo esta cubierto por HOST-045 (arista poco profunda) y HOST-481 (cara q12 y cobertura CPU/contigua); 087 y 091 se revisaron en WinUAE. La cadencia sigue siendo deuda de P8.

## 6. Experimento de lote de líneas (2026-10-24)

La conclusión inicial (el cuello era la línea) era incompleta: el cuello era el **camino `FramePlan`/`Surface`**, no el Blitter. La ruta directa `retro::wire_lines` llega a 30,57 fps release y es la que queda en la API; el lote de jobs se retiró por innecesario.
