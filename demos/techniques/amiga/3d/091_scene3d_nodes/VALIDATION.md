# Validación de la demo 091 — Scene3D retenida

Estado: **FUNCIONAL, VISIÓN OK, NO VERIFICADA por cadencia**. La escena jerárquica, la rotación, los materiales y el HUD funcionan; el presupuesto de 25 fps no se cumple con la ruta de líneas por Blitter actual (7,85 fps) y queda como deuda de P8/P5. No se han medido flicker ni el peor caso de todas las poses.

## 1. Alcance y evidencia

| Elemento | Evidencia | Veredicto |
|---|---|---|
| `Scene` retenida, handles generacionales, jerarquía y rotación | HOST-488 (`tests/host/graphics/488_scene3d_api`) | **OK en host** |
| `ModelTable` + `Model3DAssetView` + `cook-model3d` | HOST-488 + `node dist/tools/assets/cook-model3d.js --self-test` | **OK en host** |
| `FaceStyle` + patrón CPU + `CameraView` + `render_wire`/`clip_camera_segment` | HOST-488 | **OK en host** |
| Demo en WinUAE: dos cubos de alambre, jerarquía y rotación | Secuencia de 4 frames + [`src/VISION.md`](src/VISION.md) | **OK en visión** |
| HUD de host (nombre, versión, frames, fps, telemetría) | Capturas de la secuencia | **OK** |
| Cadencia | `measure-fps.mjs` | **ABIERTA: 7,85 fps / 6,37 campos** |
| Flicker/tearing | — | **SIN VERIFICAR** |
| Variante rellena (`K091_WIRE=0`) | Medida previa con 3 cubos: 6,28 fps; no re-medida con 2 | **NO VERIFICADA** |

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
| Relleno por cara (`K091_WIRE=0`, medida previa) | 3 | 1 | 1.128.714 ciclos | 7,96 | 6,28 |
| Alambre CPU (`draw_line` sin plan) | 3 | 1 | 2.392.418 ciclos | 16,86 | 2,97 |
| Alambre Blitter Copy | 2 | 1 | 964.533 ciclos | 6,80 | 7,35 |
| **Alambre Blitter EOR (por defecto)** | **2** | **2** | **903.554 ciclos** | **6,37** | **7,85** |

La cadencia es uniforme dentro de la ventana (7,47–7,99 fps), sin picos; el coste está dominado por los 96 jobs de línea (24 aristas × 4 planos) y el arranque/espera por job. La ruta EOR es ligeramente más barata que Copy (903k vs 964k) pero no cambia el orden de magnitud. El doble buffer evita que la captura vea el clear; no cambia el coste.

## 4. Visión (Ollama, prompt canónico)

`src/VISION.md` (generado con `vision-final.mjs`, 4 frames en orden temporal) describe dos cubos tridimensionales de **líneas blancas discontinuas** sobre fondo negro, el superior más grande e inclinado y el inferior más pequeño y en órbita; **no observa halos, bandas rectangulares, restos de posiciones anteriores ni píxeles cortados**. El modelo lee el HUD (`091 SCENE3D NODES`, frames y fps) y confunde algún dígito del segundo renglón; es una limitación de OCR, no del render. La línea `ONEDOT` del modo línea del Blitter produce el aspecto punteado de las aristas diagonales; es la técnica documentada, no un defecto.

## 5. Límites y siguientes gates

- **Cadencia**: 7,85 fps no cumple el suelo de 25 fps; la demo queda abierta. El siguiente paso de rendimiento es P8/P5 (batch de líneas, menos jobs por arista, o una ruta de alambre CPU/Blitter que no pague el arranque por job).
- **Flicker**: no se ha ejecutado `--flicker --require-flicker-ok`; el doble buffer reduce el riesgo, pero no hay declaración.
- **Poses**: la secuencia cubre cuatro poses; no barre todos los ángulos ni el cruce por near del hijo.
- **Variante rellena**: la medida de 6,28 fps corresponde a 3 cubos y 1 buffer; no se ha re-medido con 2 cubos ni con el doble buffer.
