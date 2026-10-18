# VALIDATION — 080_camera_pan_window (F1: paneo de cámara con frames intermedios)

Estado: **ABIERTA / NO VERIFICADA** (contrato de pixel y mecanismo probados; el display ya cumple su suelo — ≥25 Hz medidos con K adaptativo — pero la base de F1 (Full ≥12,5 Hz) aún no; ver §5 y §7). Los suelos vigentes de la fase son **base ≥12,5 Hz / display ≥25 Hz** (sustituyen al objetivo original de 50 Hz de display).

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

Salida (resumen del 2026-10-18): telemetría de cada frame == reloj esperado (Full/Fake y offset exactos), 3 pares de contrato con desplazamiento dominante k=1 (0-3 pares exactos según la fase de captura; el resto con la costura de captura ≤ 2 200 px / 3,3 %), y Fulls consecutivos con desplazamiento dominante k=2 (la escena avanza con la cámara). La costura de captura es la firma del **frame que desborda el campo** (el parche del siguiente frame cae a mitad del campo visible): es la evidencia del hueco de cadencia (§5). Evidencia del build con **política fija** (racha 1): con la política adaptativa (`K` por coste medido) el verificador `verify-080-pan-window.mjs` ya no replica la planificación, solo **invariantes** (offsets 1..15, `pan == lastFullPan + off`, rachas ≤4, Full al menos cada 12 frames) y el contrato por el **offset reportado** en la telemetría; recapturar esa secuencia está bloqueado por la captura por paso (DT-033).

## 3. Visión (Ollama, regla de oro)

- `src/VISION.md` (prompt canónico **actual**, que mira contornos; `vision-final.mjs`, 5 frames de la captura por intervalo del build adaptativo): el modelo describe el interior de la sala como un patrón de bandas geométricas (azul/verde/marrón) y **cuatro columnas verticales moviéndose hacia la izquierda** entre imágenes (incluso la reaparición de una columna desde el margen — el desplazamiento de ventana), con HUD `FULL 12.5 + FAKE 25` y `K 2`; sobre los objetos en movimiento concluye **contornos limpios, sin halos ni restos de la posición anterior**. La reevaluación **con captura por paso** (frames nítidos, sin el banding del Full mid-render) sigue pendiente: la captura por paso está bloqueada (DT-033).
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

Receta (importante: hay que cerrar instancias `winuae-gdb` huérfanas antes, y lanzar la demo justo antes del perfil — si no, el perfil lee una instancia zombi y reporta «1 frames»/`dFrame=0`; DT-033):

```bash
bash tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window >/dev/null 2>&1
node tools/debug/profile.mjs 080_camera_pan_window A500_debug 5
```

Build actual (política adaptativa, `-O3`, HUD cada 32 frames):

- Display: **139–167 frames / 5–6 s ≈ 27,8 fps** (1,79–1,80 campos/frame) → **suelo de display ≥25 Hz cumplido**.
- `full` (≈0,3 llamadas/frame): **426k–551k ciclos (≈3,0–3,9 campos)**, con `fill` de 106k–310k, proyección+clip+submit el resto y `hud` ~10–34k/frame (ya repartido por el refresh a 32 frames).
- Base: con `full` ≈ 3,4 campos, `affordable_fakes` da **K=2** (ciclo Full + 2 Fakes ≈ 3 campos de 4 usados) → base ≈ **8–9 Hz**, por debajo de los 12,5 Hz exigidos. **Cerrar F1 exige `full` ≤ 3,0 campos** (K=1 con ciclo de 4 campos = 12,5 Hz de base / 25 Hz de display).
- Control `SKIP_FILL` (ablación): 152 frames / 4 s = **38 fps**, 1,32 campos/frame — el relleno por plano es el grueso del Full; los candidatos de optimización están ahí (ver DT-034).

Objetivo: **base (Full) ≥12,5 Hz y display ≥25 Hz**. Display cumplido; base pendiente de la campaña de optimización del Full.

## 6. Qué cubre esta validación y qué NO

- **Cubierto**: exactitud del desplazamiento de ventana (§1); política Full/Fake (en la versión fija) y telemetría por frame (§2); contenido y HUD por visión (build con política fija, §3); costes desglosados del build adaptativo (§5); suelo de display ≥25 Hz (§5).
- **No cubierto / SIN VERIFICAR**: **base ≥12,5 Hz** (full ≈3,4 campos, hace falta ≤3,0; DT-034); secuencia de contrato **con la política adaptativa** y reevaluación de visión del build actual (captura por paso bloqueada, DT-033); ausencia de tearing mientras el frame desborde; paneo vertical (F3 del roadmap) y horizonte Copper (F4), fuera del alcance de esta demo.

## 7. Deuda registrada (ver `docs/guides/roadmap/ROADMAP_DEUDA_TECNICA.md`)

- **DT-032**: `PatchHandle` (`Scheduler::patchable`) escribe a través del emisor del `Plan` y, en una escena que **no llama a `Scene::commit()` por frame**, acaba corrompiendo la palabra de opcode de la copperlist activa (síntoma: frame efectivamente colgado, ~0,2 fps, word0 de la lista corrupto). Mitigación en la 080: parchear con `move_at` + `copper::patch_data(active_span())`. El contrato de `patchable` debe documentarse o robustecerse.
- **DT-033**: tooling de medición/captura de la 080: (a) `measure-fps.mjs` aborta con `dFrame=0` para este binario (mientras `profile.mjs` cuenta frames y ciclos correctamente); (b) instancias `winuae-gdb` huérfanas hacen que el perfil lea una instancia zombi («1 frames»/`dFrame=0`): cerrar las propias y lanzar la demo **justo antes** del perfil; (c) la captura por paso (`--sequence-step-frames`) aborta con «GDB socket is closed … dropped after a timeout» en `captureScreenshot` (`run-demo.js:282`), lo que bloquea recapturar la secuencia de contrato con la política adaptativa (la captura por intervalo `--sequence-frames N --sequence-interval-ms` sí funciona).
- **DT-034**: presupuesto del relleno por plano (~106k–310k por Full medido; control `SKIP_FILL` = 38 fps) incompatible con la base de F1 (`full` ≤3,0 campos para K=1); requiere campaña de optimización (o rediseño de la pasada de sala) antes de cerrar F1. Candidatos medidos: reparto del relleno de paredes (líneas EOR compartidas), dedupe de aristas, caja de sala/paneles más ajustada.
