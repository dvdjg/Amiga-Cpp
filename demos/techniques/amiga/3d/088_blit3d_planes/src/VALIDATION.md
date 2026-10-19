# VALIDATION — 088 BLIT3D PLANES

## Alcance

Cubo convexo rotando sobre 2 bitplanes, relleno por plano con Blitter (contorno ONEDOT + area fill acotado al bbox + patrón opcional), **culling por normal 3D general para cualquier convexo** (caras CCW desde fuera; cámara en el origen), **fast path de frustum por objeto** y **proyección única de los 8 vértices**, **clear del rectángulo sucio** (unión de los bbox N-1/N-2/actual) y **recorte de cada cara al rectángulo seguro del contorno** antes de planificar las aristas. Se verifica: cubo con 3 caras de colores y relleno limpio, coste debug, el **clip fino en hardware** con el modo deriva (`kDrift`) **sin residuo en el borde**, y la equivalencia Blitter/CPU del camino por plano. **No cubierto**: bloqueo a 50 Hz, solapes entre sólidos (painter, no z-buffer), la costura de 1 px de arista compartida entre planos y la escritura fantasma de la fila 0 (defectos conocidos, ver abajo).

## Medidas

`node tools/debug/measure-fps.mjs 088_blit3d_planes --config <cfg> --duration 5000`.

| Config | fps | ciclos/frame | campos/frame | ventanas |
|---|---|---|---|---|
| `A500_debug` (centrado, defecto) | 32.44 | 218 668 | 1.54 | 29.1–37.0 |
| `A500_release` (centrado, defecto) | 33.00 | 214 958 | 1.52 | 29.7–37.3 |
| `A500_debug` (`kDrift = true`) | 27.28 | 260 049 | 1.83 | 25.2–29.5 |

El recorte de caras cuesta ~0.4 % en debug (32.56 → 32.44, dentro del ruido del medidor; en release la ventana de 5 s dio 33.0 frente a 33.9 previos, también dentro de la dispersión de ±4 fps). **Cadencia**: alterna frames de 1 y 2 campos (29–37 fps, dispersión ~8 en la ventana de 5 s); no hay frames de 3+ campos; el suelo de 25 fps se cumple en la deriva. El relleno (≈65 %) sigue siendo el coste dominante.

## Bug del borde derecho (CERRADO)

**Síntoma**: con el clip lateral (`kDrift`), algunos frames mostraban una **columna suelta en `x≈317–319`** con el cubo lejos del borde derecho, y persistía en frames posteriores (visión: `qwen3-vl` no la veía; la inspección de píxeles sí).

**Causa raíz** (aislada con la sonda estática `L1-003` y su modo “solo contornos”, no con el cubo rotando):
1. El **trazador de líneas del Blitter escribe cada píxel un paso por detrás** de la posición que lee (el primer píxel va por el canal D y el resto por C, que ya avanzó). Una vertical dada en `x` se dibuja en `x-1`.
2. La cara recortada por el borde **izquierdo** tiene una vertical en `x=0` → se dibuja en `x=-1` → `(u32(-1)>>3)&~1` cae en la **cola de la fila anterior** (`x≈width-1`), fuera del bbox de la cara.
3. El clear por rectángulo sucio cubre el bbox de las caras, no esa cola → el píxel queda como **residuo** que reaparece en los frames en que el cubo está a la izquierda (y alterna entre los dos buffers del doble buffer).

**Fix**: el relleno por plano recorta cada cara al **rectángulo seguro del contorno** `[1, width-1] × [0, height-1]` (`clip_face_to_rect`/`plan_plane_edges_in_bounds`). El margen izquierdo de 1 px absorbe el desfase del trazador, la última columna/fila es el último píxel incluido y el contorno queda cerrado en el borde (la paridad del `FILL_XOR` se mantiene). Coste: una columna de 1 px sin pintar en el borde de la pantalla (documentado).

**Evidencia**:
- `HOST-482` (`tests/host/graphics/482_plane_fill_in_bounds`): recorte genérico (s16 y s32), ningún vértice/arista fuera del rectángulo seguro, arista compartida deduplicada tras recortar.
- `L1-003` (`tests/amiga/l1_backend/003_plane_fill_bounds`): sonda **estática** con caras en `x=320`, `x=-1`, `y=232` y una fuera; compara contra la referencia CPU y publica 5 bits en `detail`. A/B: con `min_x=1` → `detail & 0x1f == 0x1f` (OK); con `min_x=0` (comportamiento anterior) → `detail == 2` (FAIL, con la vertical en `x≈width-1`).
- Secuencia de la 088 con `kDrift`: los frames con el cubo a la izquierda ya **no tienen ningún píxel** en `x≥310`; los que cruzan el borde muestran el perfil triangular legítimo de la cara (2–4 px en `x=319`), sin columna aislada.

## Defectos conocidos (abiertos, separados)

- **Escritura fantasma de la fila 0**: el primer píxel de cada línea va por el canal D, programado a la base del plano (`blitter_line_draw(eor, plane.data(), plane.data())`), y deposita una palabra de la fila de la línea en la fila 0 (`x≈0–15`). El clear por rectángulo sucio no la cubre. Fix propuesto: apuntar D a una palabra de descarte fuera del bitmap visible (scratch de 2 bytes Chip del backend) — requiere ampliar la API del fill con ese scratch. `L1-003` lo reporta como bit 5 (informativo).
- **Costura de 1 px magenta** entre caras que comparten arista y caen en planos distintos: cada plano posee el píxel del borde. Pendiente la regla top-left/half-open por cara.

## Visión (Ollama, prompt canónico)

`src/VISION.md` (4 frames, `qwen3-vl:8b-instruct-q8_0`): cubo rotando, planos rojo/azul con contorno **limpio, sin halos, sin píxeles cortados ni colgando**, fondo negro uniforme y HUD (`088 BLIT3D PLANES`, versión, contador `F<num>`).

## Comandos

```bash
bash tools/build/build-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --debug
bash tools/run/run-demo.sh demos/techniques/amiga/3d/088_blit3d_planes --sequence-frames 4 --sequence-interval-ms 900
node tools/debug/measure-fps.mjs 088_blit3d_planes --config A500_debug --duration 5000
node tools/analyze/vision-final.mjs 088_blit3d_planes out/run/088_blit3d_planes/A500_debug/sequence --demo-dir demos/techniques/amiga/3d/088_blit3d_planes
bash tools/run-host-tests.sh tests/host/graphics/482_plane_fill_in_bounds
bash tests/amiga/l1_backend/003_plane_fill_bounds/verify.sh
```
