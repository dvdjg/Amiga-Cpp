# VALIDATION — 088 BLIT3D PLANES

## Alcance

Cubo convexo rotando sobre 2 bitplanes, relleno por plano con Blitter (contorno ONEDOT + area fill acotado al bbox + patrón opcional), **culling por normal 3D general para cualquier convexo** (caras CCW desde fuera; cámara en el origen), **fast path de frustum por objeto** y **proyección única de los 8 vértices**, **clear del rectángulo sucio** (unión de los bbox N-1/N-2/actual) y **recorte de cada cara al rectángulo seguro del contorno** antes de planificar las aristas, con **palabra de descarte** para el primer píxel del trazador. Se verifica: cubo con 3 caras de colores y relleno limpio, coste debug, el **clip fino en hardware** con el modo deriva (`kDrift`) **sin residuo en el borde**, y la equivalencia Blitter/CPU del camino por plano. **No cubierto**: bloqueo a 50 Hz y solapes entre sólidos (painter, no z-buffer).

## Medidas

`node tools/debug/measure-fps.mjs 088_blit3d_planes --config <cfg> --duration 5000`.

| Config | fps | ciclos/frame | campos/frame | ventanas |
|---|---|---|---|---|
| `A500_debug` (centrado, defecto) | 32.33 | 219 435 | 1.55 | 28.3–37.0 |
| `A500_release` (centrado, defecto) | 32.92 | 215 500 | 1.52 | 29.5–37.2 |
| `A500_debug` (`kDrift = true`) | 27.28 | 260 049 | 1.83 | 25.2–29.5 |

El recorte de caras y la palabra de descarte no tienen coste medible en debug (32.56 → 32.33, dentro del ruido del medidor; en release la ventana de 20 s con 8 muestras da 32.92 frente a 33.87 previos, dentro de la dispersión de ±4 fps). **Cadencia**: alterna frames de 1 y 2 campos (29–37 fps, dispersión ~8 en la ventana de 5 s); no hay frames de 3+ campos; el suelo de 25 fps se cumple en la deriva. El relleno (≈65 %) sigue siendo el coste dominante.

## Perfil por secciones (debug)

`node tools/debug/profile.mjs 088_blit3d_planes A500_debug` (175 frames, 204 486 ciclos/frame, 1.44 campos):

| Sección | ciclos/frame | % |
|---|---|---|
| `actors` (transform + test de frustum) | 41 276 | 20.2 % |
| `blits` (culling/proyección + builder) | 18 937 | 9.3 % |
| `copper` (**relleno**: clear + contorno + area fill + esperas) | 123 348 | 60.3 % |
| resto (espera de VBlank + bucle) | 20 925 | 10.2 % |

El relleno sigue siendo el coste dominante (60 %); la siguiente campaña de optimización va ahí. `profile.mjs` usa ahora el WinUAE del repo (`WINUAE_GDB_DIR` → `../WinUAE-DBG/bin`) con la misma prioridad que el runner; antes lanzaba el stock de la extensión y el arranque fallaba.

## Perfil por secciones (debug)

`node tools/debug/profile.mjs 088_blit3d_planes A500_debug` (175 frames, 204 486 ciclos/frame, 1.44 campos):

| Sección | ciclos/frame | % |
|---|---|---|
| `actors` (transform + test de frustum) | 41 276 | 20.2 % |
| `blits` (culling/proyección + builder) | 18 937 | 9.3 % |
| `copper` (**relleno**: clear + contorno + area fill + esperas) | 123 348 | 60.3 % |
| resto (espera de VBlank + bucle) | 20 925 | 10.2 % |

El relleno es el coste dominante. Con las subsecciones del engine (`fill_clear` 10, `fill_lines` 11, `fill_area` 12, siempre activas) el reparto interno medido es **clear ≈ 30 %, contorno ≈ 47 %, area fill ≈ 22 %**: el objetivo son las esperas por arista del contorno (una por línea ONEDOT), no el área.

## Coste del pipeline de CPU (`actors`) — sonda L1-004

`tests/amiga/l1_backend/004_actors_cost` mide por vértice y repetición (determinista, 64 repeticiones, contador de ciclos del periférico):

| Fase | ciclos/vértice (antes) | ciclos/vértice (después) |
|---|---|---|
| `mesh_transform` (+ `load_rotate`) | 1 312 | 1 312 |
| `camera_contains_vertices` | 1 724 | **1 524** |
| `project_camera_vertex` | 804 | 804 |

Fix aplicado: `Plane::dist` sumaba `inner(d, one)` (un `muls.w` por plano y vértice que multiplica por 1); ahora suma `d` directamente (equivalente exacto en la escala cruda). A/B: −11.6 % en el test de frustum y la sección `actors` de la 088 baja ~1.6k ciclos/frame. El resto del coste (transform ~1.3k y proyección ~0.8k por vértice) es aritmética de la representación (`Fixed<s16,0>`/`divs.w`) y se atacaría con un pipeline por lotes o una ruta por columna, no con microcambios.

## Cadencia

La ventana de 5–20 s da **1.37–1.55 campos/frame** (29–37 fps, dispersión ~8): **alterna frames de 1 y 2 campos**, sin frames de 3+ campos y con el suelo de 25 fps cumplido. No se «bloquea a 1 campo» porque el trabajo real (~195–205k ciclos/frame) supera el campo PAL (~142k): el camino a 50 Hz es **solapar CPU y Blitter** (encadenar el relleno por IRQ y hacer trabajo de CPU durante los blits), no recortar la medida. Es el siguiente paso; hasta entonces la alternancia es el comportamiento esperado y así queda documentada.

## Optimización del relleno (análisis medido; sin cambio aplicado)

Con las subsecciones permanentes del engine (`fill_clear`, `fill_lines`, `fill_area`), el relleno de la 088 se reparte en **clear ≈ 30 %, contorno ≈ 47 %, area fill ≈ 22 %**. Conclusiones del análisis:

- **Cadena async por IRQ del fill completo** (clear + líneas + area fill en un `FramePlan` lanzado con `execute_frame_plan_async`): se implementó y se validó *estáticamente* en L1-003 (la cadena produce el mismo resultado que la referencia CPU), pero **se descartó**: cada job encadenado paga el arranque del Blitter + la IRQ de nivel 3 + la reprogramación en la ISR (~2,5–4k), y con ~10 líneas por frame el sobrecoste (~50k) supera cualquier solape. Es el mismo veredicto ya medido en la 116 (DT-026: 8,9 fps con 51 líneas en async vs 18,05 en síncrono). Detalle en `blitter-line-subpixel-fill.md` §5.
- **Fusión de aristas colineales**: no aplica al cubo (las aristas visibles no son colineales; la dedupe de paridad ya elimina las compartidas del mismo plano).
- **Una máscara por plano** (camino `fill_polygons_by_plane_spans`): medido 4× más lento en este objeto (los spans de CPU no amortizan).
- **Clear**: ya es solo el rectángulo sucio (unión de N-1/N-2/actual, el mínimo seguro con doble buffer); barrer el plano completo costaría ~2×.
- **Siguiente palanca real**: el *contorno* está serializado por el hardware (una línea por blit, con su espera) y el *coste por línea* (~2,7k con contención de bus) no baja con el pipeline actual; para 50 Hz habría que reducir el trabajo de CPU (transform 1,3k/vértice, frustum 1,5k/vértice) o cambiar el algoritmo de relleno (una máscara por plano generada por hardware), no microoptimizar el clear o el área.

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

## Escritura fantasma de la fila 0 (CERRADO)

En modo línea el **primer píxel** de cada arista va por el canal **D**; con `D = plane.data()` la palabra modificada aterrizaba en la **fila 0** del bitmap visible (`x≈0–15`), fuera del rectángulo sucio, y quedaba como píxeles sueltos en la esquina superior izquierda (visto en los volcados de planos de la 088 y en `L1-003`). Fix: el backend reserva una **palabra de descarte de 2 B en Chip** (`m_line_trash`, en `configure_memory`) y el relleno por plano la pasa como `d_base` del trazador; se machaca en cada línea y no la lee nadie. `L1-003` la comprueba (bit 5: fila 0 limpia) y el test pasa `detail == 0x3f`.

## Defectos conocidos (abiertos)

- **Costura de arista compartida entre planos**: verificada en L1-003 para adyacencia **recta** (bits 6/7: la columna compartida es de un solo color y la junta no tiene hueco) y **diagonal** (bit 8: dos triángulos que comparten la diagonal, sin magenta ni hueco en toda la banda). El «magenta de esquina» que citaba la validación anterior era la propia cara magenta, no un solape.

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
