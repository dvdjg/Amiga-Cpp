# VALIDATION — 080_camera_pan_window (F1: paneo de cámara con frames intermedios)

Estado: **ABIERTA / NO VERIFICADA** (contrato de pixel y mecanismo probados; la cadencia de display objetivo de F1 — 50 Hz con Full a 25 Hz — no se alcanza todavía; ver §5 y §7).

## 1. Contrato del mecanismo: prueba de aislamiento (exactitud)

Objetivo: demostrar que el desplazamiento de la ventana (punteros `BPLxPT` + `BPLCON1` con `fine_scroll_coarse`/`fine_delay`) mueve la imagen **exactamente** los píxeles pedidos.

```bash
printf 'DEMO_FLAGS=-DK080_OFFSET_TEST=1\n' > demos/techniques/amiga/3d/080_camera_pan_window/build.args
bash tools/build/build-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --debug --clean
bash tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --sequence-step-frames 136
# comparar grupos de 8 frames (offsets 0..16) contra el grupo del offset 0 (script ad-hoc
# de band-diff pixel a pixel sobre la banda de escena)
```

Con la cámara congelada (`pan=-128`) y el ciclo de offsets forzados 0..16:

| Offset pedido | Desplazamiento visible medido | Discordancias |
|---|---|---|
| 0..15 (grupos consecutivos) | 0..15 (continuo, +1 px por grupo) | **0 / 66352** en cada grupo |
| 16 (cruce de palabra: coarse +16 px, fino 0) | 15→16 según el borde del grupo (ver nota) | 0 |

Nota: el PNG capturado muestra el campo anterior a la pausa (lag de 1 campo documentado en el runner); tras alinear ese desfase, el mapeo resulta **continuo y exacto** (cada posición de ventana produce el desplazamiento pedido, sin saltos ni dientes de sierra de 16 px). La corrupción inicial de word0 de la copperlist (slots de parche inválidos en `takeover`) quedó corregida registrando el par de punteros con `Scene::set_plane_patch`.

## 2. Contrato en la secuencia Full/Fake

```bash
bash tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --sequence-step-frames 16
node tools/analyze/verify-080-pan-window.mjs --sequence-dir out/run/080_camera_pan_window/A500_debug/sequence
```

Salida (resumen del 2026-10-18): telemetría de cada frame == reloj esperado (Full/Fake y offset exactos), 3 pares de contrato con desplazamiento dominante k=1 (0-3 pares exactos según la fase de captura; el resto con la costura de captura ≤ 2 200 px / 3,3 %), y Fulls consecutivos con desplazamiento dominante k=2 (la escena avanza con la cámara). La costura de captura es la firma del **frame que desborda el campo** (el parche del siguiente frame cae a mitad del campo visible): es la evidencia del hueco de cadencia (§5).

## 3. Visión (Ollama, regla de oro)

- `src/VISION.md` (prompt canónico, `vision-final.mjs`, 5 frames): describe la escena correctamente — fondo azul oscuro, "ventana" con un triángulo verde (pared izquierda), suelo marrón y **cuatro barras verticales** (2 cian, amarilla, naranja) — y el HUD con `080 CAMERA PAN WINDOW` / `FULL 25HZ + FAKE 50HZ` / contadores y `FULL/FAKE OFF … PAN …`. Sin artefactos reportados en la captura estática.
- `ollama-frames.mjs` sobre una secuencia: confirma **movimiento apreciable** ("la escena se ha movido hacia la izquierda, revelando partes nuevas") — la dinámica del paneo es observable por el modelo.

## 4. Gates técnicos

| Gate | Resultado |
|---|---|
| build (debug) | OK |
| run/analyze (READY + captura) | OK |
| Contrato de pixel de secuencia (§2) | OK (costura de captura acotada; exactitud en §1) |
| Flicker (`test-regression.sh --flicker --require-flicker-ok`) | **sin veredicto**: la captura de secuencia del runner abortó con error de protocolo GDB (herramienta); la costura de §2 indica tearing mientras el frame desborde el campo |
| `measure-fps.mjs` | **herramienta**: reporta `dFrame=0` («demo colgada») para este binario; la cadencia se midió con `profile.mjs` (§5) |
| Visión (canónica) | OK (§3) |

## 5. Cadencia (medida con `profile.mjs`)

```bash
node tools/debug/profile.mjs 080_camera_pan_window A500_debug 4
```

- Total: **≈ 314 000–326 000 ciclos/frame ≈ 2,22–2,30 campos/frame (~22 fps)** en debug.
- Por sección (calls/frame → coste por llamada):
  - `full` (0,5/frame ≈ por Full): **467–585k ciclos** (3,3–4,1 campos) — pasada completa: proyección+clip+submit (~120k) + relleno por plano (~113–327k según la caja del panel).
  - `fill` (los dos `fill_polygons_by_plane` por Full): 113–327k.
  - `patch` (~1,4k/frame): parcheo de 4 pares `BPLxPT` + `BPLCON1` (despreciable).
  - `hud` (cada 16 frames): ~373k por actualización (escritura del texto por palabras con el display activo; por eso se refresca a ~3 Hz y no por frame).
- **Objetivo F1 (50 Hz de display con Full a 25 Hz)** exige Full ≤ 1 campo (≈142k) por Full cuando alterna con Fakes; medido: Full ≈ 3,3–4,1 campos → la alternancia produce ~2,2 campos/frame y **no se cumple**. El frame desborda el VBlank y el parche cae a mitad de campo (firma: costura en §2), lo que además impediría un gate de flicker limpio.
- El floor de 25 fps para demos de relleno de polígonos queda también ligeramente por debajo en esta medida (22 fps).

## 6. Qué cubre esta validación y qué NO

- **Cubierto**: exactitud del desplazamiento de ventana (§1); política Full/Fake, offsets y telemetría por frame (§2); contenido y HUD por visión (§3); coste desglosado (§5).
- **No cubierto / SIN VERIFICAR**: cadencia objetivo de F1 (50 Hz de display) y ausencia de tearing mientras el frame desborde; gate de flicker (herramienta abortada); paneo vertical (F3 del roadmap) y horizonte Copper (F4), fuera del alcance de esta demo.

## 7. Deuda registrada (ver `docs/guides/roadmap/ROADMAP_DEUDA_TECNICA.md`)

- **DT-032**: `PatchHandle` (`Scheduler::patchable`) escribe a través del emisor del `Plan` y, en una escena que **no llama a `Scene::commit()` por frame**, acaba corrompiendo la palabra de opcode de la copperlist activa (síntoma: frame efectivamente colgado, ~0,2 fps, word0 de la lista corrupto). Mitigación en la 080: parchear con `move_at` + `copper::patch_data(active_span())`. El contrato de `patchable` debe documentarse o robustecerse.
- **DT-033**: `measure-fps.mjs` aborta con `dFrame=0` para el binario de la 080 (mientras `profile.mjs` cuenta frames y ciclos correctamente): revisar la resolución/lectura del run-status por canal lateral para este caso.
- **DT-034**: presupuesto del relleno por plano a pantalla completa (2 planos de sala, ~113–327k por Full) incompatible con el objetivo de 1 campo/frame de F1; requiere campaña de optimización (o rediseño de la pasada de sala) antes de cerrar F1.
