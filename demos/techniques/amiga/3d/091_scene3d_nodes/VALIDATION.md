# Validacion de la demo 091 - Scene3D retenida

Estado: **FUNCIONAL, VISION OK, NO VERIFICADA por cadencia**. La escena jerarquica, la rotacion, los materiales y el HUD funcionan; el presupuesto de 25 fps no se cumple con la ruta de lineas por Blitter actual (22,07 fps) ni con la variante rellena (8,39 fps), y queda como deuda de P8/P5. No se han medido flicker ni el peor caso de todas las poses.

## 1. Alcance y evidencia

| Elemento | Evidencia | Veredicto |
|---|---|---|
| `Scene` retenida, handles generacionales, jerarquia y rotacion | HOST-488 (`tests/host/graphics/488_scene3d_api`) | **OK en host** |
| `ModelTable` + `Model3DAssetView` + `cook-model3d` | HOST-488 + `node dist/tools/assets/cook-model3d.js --self-test` | **OK en host** |
| `FaceStyle` + patron CPU + `CameraView` + `render_wire`/`clip_camera_segment` | HOST-488 | **OK en host** |
| Demo en WinUAE: dos cubos de alambre, jerarquia y rotacion | Secuencia de 4 frames + [`src/VISION.md`](src/VISION.md) | **OK en vision** |
| HUD de host (nombre, version, frames, fps, telemetria) | Capturas de la secuencia | **OK** |
| Cadencia de la variante por defecto (1 plano, aristas visibles) | `measure-fps.mjs` | **ABIERTA: 22,07 fps / 2,26 campos** |
| Flicker/tearing | - | **SIN VERIFICAR** |
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
| **Alambre Blitter EOR, 1 plano, 18 aristas visibles (por defecto)** | **2** | **2** | **321.398 ciclos** | **2,26** | **22,07** |

La cadencia de la variante por defecto es uniforme dentro de la ventana (21,6-22,6 fps), sin picos. La ruta optimizada usa 1 plano, culling de aristas traseras (18 aristas visibles), mascara de deduplicacion O(1) y clear asincrono; quedan 18 jobs de linea por frame. Las filas de CPU/Copy son medidas historicas de la ruta de 4 planos. El doble buffer evita que la captura vea el clear; no cambia el coste. La variante rellena mejora a 8,39 fps al reducir a 2 cubos y usar doble buffer, pero sigue por debajo del suelo de 25 fps.

## 4. Vision (Ollama, prompt canonico)

`src/VISION.md` (generado con `vision-final.mjs`, 4 frames en orden temporal) describe dos cubos tridimensionales de **lineas blancas discontinuas** sobre fondo negro, el superior mas grande e inclinado y el inferior mas pequeno y en orbita; **no observa halos, bandas rectangulares, restos de posiciones anteriores ni pixeles cortados**. El modelo lee el HUD (`091 SCENE3D NODES`, frames y fps) y confunde algun digito del segundo renglon; es una limitacion de OCR, no del render. La linea `ONEDOT` del modo linea del Blitter produce el aspecto punteado de las aristas diagonales; es la tecnica documentada, no un defecto.

La secuencia de la **variante rellena** se valido aparte con `vision-final.mjs` en un directorio temporal (3 frames): describe dos cubos solidos, el cian mayor y el hijo pequeno, con **contornos limpios, sin huecos ni muescas**, y confirma que el fondo negro queda visible alrededor. Es la evidencia visual del arreglo del DDA de `convex_spans`.

## 5. Limites y siguientes gates

- **Cadencia**: 22,07 fps (alambre), 8,39 fps (relleno por cara) y 12,64 fps (silueta convexa) no cumplen el suelo de 25 fps; el alambre se acerca (18 jobs, 321k ciclos) pero la demo queda abierta. La silueta mejora 1,5x (561.192 vs 845.925 ciclos) al hacer 1 fill por objeto, pero aun paga 4 planos por objeto y el clear. El siguiente paso de P8/P5 es la composicion `S` + `M_p`, el clear por dirty-rect y el router por coste; tambien falta el batch de lineas para el alambre.
- **Flicker**: no se ha ejecutado `--flicker --require-flicker-ok`; el doble buffer reduce el riesgo, pero no hay declaracion.
- **Poses**: la secuencia cubre cuatro poses; no barre todos los angulos ni el cruce por near del hijo.
- **Regresion corregida**: `convex_spans` avanzaba como maximo 1 px por fila en aristas con `|dx| > dy` (DDA con `if` en vez de `while`), lo que dejaba muescas en caras y cubos rellenos. El arreglo esta cubierto por HOST-045 (arista poco profunda) y HOST-481 (cara q12 y cobertura CPU/contigua); 087 y 091 se revisaron en WinUAE. La cadencia sigue siendo deuda de P8.
