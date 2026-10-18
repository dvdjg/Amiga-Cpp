# VALIDATION — 080_camera_pan_window (F1: paneo de cámara con frames intermedios)

Estado: **ABIERTA / NO VERIFICADA** por cadencia (display ≥25 Hz cumplido; base de F1 ≥12,5 Hz pendiente, §5). El **render de la escena está verificado correcto** (§3, pase C con los elementos del `INTENT.md`) tras arreglar la rotura observada 2026-10-18: era una **miscompilación con `-O3` en el TU de la demo** (DT-038), no un fallo de la lógica — arreglada con `DEMO_OPT=-O2`.

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

- `src/VISION.md` (prompt canónico **actual**, que mira contornos; `vision-final.mjs`, 5 frames de la captura por intervalo del build con patrón): el modelo describe el fondo como un patrón de bandas geométricas y **barras verticales que se desplazan** entre imágenes (con la reaparición de una columna desde el margen — el desplazamiento de ventana) y el HUD `FULL 12.5 + FAKE 25` con `K 2`; concluye **contornos limpios, sin halos ni restos de la posición anterior**, con el fondo uniforme. La reevaluación **con captura por paso** (frames nítidos, sin el banding del Full mid-render) sigue pendiente: la captura por paso está bloqueada (DT-033).
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

- Display: ≥25 Hz cumplido en las medidas de la sesión (≈27 fps con K=2).
- **Desglose del Full** (build arreglado a `-O2`, con patrón; secciones `kProfWait`/`kProfClip`; `profile.mjs`): `wait` inicial ≈ **300 ciclos** (el Blitter está ocioso entre Fulls); **CPU de la pasada** (transform+proyección+recorte+submit de las 9 caras) **69k–103k**; **`fill_polygons_by_plane`: 104k–316k POR LLAMADA**; `patch` ~15k–17k/frame. El Full lo domina el relleno por plano (clear 2 c/word + aristas + fill 6 c/word + patrón 4–6 c/word), **no el CPU** — la palanca restante es el diseño del relleno (y solapar su tiempo de Blitter con CPU/Copper, F2), no microoptimizar el recorte.
- `full` (≈0,3 llamadas/frame): **506k–559k ciclos con el damero de mezcla** en el build arreglado (`-O2`); `hud` ~12k/frame (repartido por el refresh a 32 frames). Nota: las cifras anteriores de esta sección (426k–609k) se midieron en el build con `-O3`, que estaba **mal compilado** (DT-038); las de arriba son las del build vigente.
- Base: con `full` ≈ 3,4 campos (sin patrón), `affordable_fakes` da **K=2** (ciclo Full + 2 Fakes) → base ≈ **8–9 Hz**, por debajo de los 12,5 Hz exigidos. **Cerrar F1 exige `full` ≤ 3,0 campos** (K=1 con ciclo de 4 campos = 12,5 Hz de base / 25 Hz de display).
- Control `SKIP_FILL` (ablación): 152 frames / 4 s = **38 fps**, 1,32 campos/frame — el relleno por plano es el grueso del Full; los candidatos de optimización están ahí (ver DT-034).
- **Patrón de sala (A/B, `profile.mjs`)**: pasada completa **426k–552k** sin patrón (`-DK080_NO_PATTERN`) vs **508k–595k** con `Checker4` en un pase de plano (A = plano de patrón, 6 c/palabra). Con la **aplicación por fases** (`pattern_period`, 4 c/palabra y clears a 2 en fases vacías) el mínimo de la sección `fill` baja de **117k a 104k** (≈ el ahorro teórico de ~2 c/palabra por fase); la comparación de `full` por llamada entre corridas tiene ruido de ±10–20k, así que la cifra fiable es la de `fill`. El barrido único de planos contiguos no aplica a este layout; el análisis completo está en `blitter-line-subpixel-fill.md` §5.

- **Dedupe de aristas pares** (engine, `plan_plane_edges`): las aristas compartidas por dos caras del mismo bit (6 en la sala) ya no se dibujan dos veces para cancelarse — se saltan. El render es **identico** (captura comparada; equivalencia de region probada bit a bit en HOST-474) y el ahorro (~6 blits de linea) no es separable del ruido del perfil en esta escena (valor general en mallas).

Objetivo: **base (Full) ≥12,5 Hz y display ≥25 Hz**. Display cumplido; base pendiente de la campaña de optimización del Full.

## 6. Qué cubre esta validación y qué NO

- **Cubierto**: exactitud del desplazamiento de ventana (§1); política Full/Fake (en la versión fija) y telemetría por frame (§2); contenido y HUD por visión (build con política fija, §3); costes desglosados del build adaptativo (§5); suelo de display ≥25 Hz (§5).
- **No cubierto / SIN VERIFICAR**: **base ≥12,5 Hz** (full ≈3,4 campos, hace falta ≤3,0; DT-034); secuencia de contrato **con la política adaptativa** y reevaluación de visión del build actual (captura por paso bloqueada, DT-033); ausencia de tearing mientras el frame desborde; paneo vertical (F3 del roadmap) y horizonte Copper (F4), fuera del alcance de esta demo.

## 7. Deuda registrada (ver `docs/guides/roadmap/ROADMAP_DEUDA_TECNICA.md`)

- **DT-032**: `PatchHandle` (`Scheduler::patchable`) escribe a través del emisor del `Plan` y, en una escena que **no llama a `Scene::commit()` por frame**, acaba corrompiendo la palabra de opcode de la copperlist activa (síntoma: frame efectivamente colgado, ~0,2 fps, word0 de la lista corrupto). Mitigación en la 080: parchear con `move_at` + `copper::patch_data(active_span())`. El contrato de `patchable` debe documentarse o robustecerse.
- **DT-033**: tooling de medición/captura de la 080: (a) `measure-fps.mjs` aborta con `dFrame=0` para este binario (mientras `profile.mjs` cuenta frames y ciclos correctamente); (b) instancias `winuae-gdb` huérfanas hacen que el perfil lea una instancia zombi («1 frames»/`dFrame=0`): cerrar las propias y lanzar la demo **justo antes** del perfil; (c) la captura por paso (`--sequence-step-frames`) aborta con «GDB socket is closed … dropped after a timeout» en `captureScreenshot` (`run-demo.js:282`), lo que bloquea recapturar la secuencia de contrato con la política adaptativa (la captura por intervalo `--sequence-frames N --sequence-interval-ms` sí funciona).
- **DT-034**: presupuesto del relleno por plano (~106k–310k por Full medido; control `SKIP_FILL` = 38 fps) incompatible con la base de F1 (`full` ≤3,0 campos para K=1); requiere campaña de optimización (o rediseño de la pasada de sala) antes de cerrar F1. Candidatos medidos: reparto del relleno de paredes (líneas EOR compartidas), dedupe de aristas, caja de sala/paneles más ajustada.

## 8. Render de la sala: rotura observada y RESUELTA (2026-10-18, DT-037 -> DT-038)

La geometría de la sala **no se renderiza correctamente**: con la cámara congelada en pan=0 (-DK080_PAN_FREEZE=0, captura estática sin tearing) el frame muestra triangulos/diagonales a medio rellenar y regiones invertidas (patron en X) en vez del tunel simetrico (techo arriba, suelo abajo, paredes en cuna, pared del fondo centrada). El sintoma ya aparecia en las descripciones antiguas de vision («un triangulo verde (pared izquierda)»): **nunca se trato como defecto** porque las validaciones comprobaban el *mecanismo* (contrato de desplazamiento con la camara congelada, §1-§2) y no la correccion de la escena.

**Acotado con evidencia**: la **referencia CPU** (ill_polygons_by_plane_cpu, mismas caras/colores/pipeline de proyeccion+recorte, reproducida en host) produce el tunel **correcto** (imagen de control en out/tmp de la sesion; el repro vive en la bitacora de la sesion). Es decir: geometria, proyeccion, recorte y paridad por plano estan bien; la divergencia esta en el **camino Blitter** (contorno EOR ONEDOT + area fill FILL_XOR) con esta configuracion de caras (quads grandes del tubo con aristas exactamente horizontales en pantalla y aristas compartidas entre caras de planos distintos). Primer experimento (sesgo de z en las esquinas compartidas, como el de los paneles) **no cambio el resultado**; el sesgo del extremo lejano resultaba sub-pixel (±4 unidades a z≈1024 ≈ 0,1 px).

**Siguiente paso**: aislamiento por cara (rellenar una sola cara por plano y volcar el plano del emulador contra la referencia CPU) para localizar la fila/arista donde la paridad diverge (DT-037).

## 9. Pase C obligatorio (coherencia con el INTENT) y cierre de la rotura (2026-10-18)

**Pase C** (protocolo §6.4 de DEMO_VISUAL_DEBUG.md, ahora obligatorio antes de VERIFICADA): pregunta con los elementos del INTENT.md (sala-tubo: suelo, techo, paredes, pared del fondo; cuatro paneles verticales; HUD) sobre 3 frames de la captura del build arreglado. Respuesta cruda completa: 
# Anexo 9 — Pase C obligatorio (coherencia con el INTENT) y cierre de la rotura (2026-10-18)

**Pase C** (protocolo §6.4 de `DEMO_VISUAL_DEBUG.md`, ahora obligatorio antes de VERIFICADA): pregunta con los elementos del `INTENT.md` (sala-tubo: suelo, techo, pared izquierda, pared derecha, pared del fondo; cuatro paneles verticales; HUD) sobre 3 frames de la captura del build arreglado. Respuesta cruda completa: `080_camera_pan_window_report.md`; resumen: **todos los elementos PRESENTES y coherentes con un túnel 3D**; «¿alguna superficie aparece partida, invertida o en triángulos sueltos? **No**. Todas las superficies están completamente formadas y no presentan cortes, inversiones ni triangulaciones visibles…»; movimiento continuo.

**Rotura observada y causa**: durante la sesión 2026-10-18 el render salió partido (triángulos/patrón en X). Acotado con (a) captura estática con la cámara congelada, (b) referencia CPU de la misma escena en host (correcta: el bug no era de geometría ni del relleno lógico) y (c) **bisección por commits con `git worktree`**: el commit original (1b32ab92) renderiza limpio; `45a056e9` rompe; la variable es el cambio de `-O2` a `-O3` en el TU de la demo (mismo código a `-O2` limpio). **Miscompilación, no lógica** (DT-038). Arreglo: `DEMO_OPT=-O2`, verificado por captura y por el pase C de arriba.

**Lecciones de método** (ya aplicadas en `DEMO_VISUAL_DEBUG.md` §5.1): el eval de visión **describe** y no sustituye el contraste contra el INTENT; «triángulo/bandas» donde el INTENT dice «túnel/superficie» es un FALLO; ante una regresión sospechosa, bisección barata con worktree antes de teorizar; un `-O3` local es una variable de render sospechosa de miscompilación.

**Vision con la mezcla (2026-10-18)**: el primer intento degenero (respuesta cruda `0000...`); causa: **instancia de Ollama atascada**, no la textura — `ollama stop qwen3-vl:8b-instruct-q8_0` y reintento resuelve (leccion anotada en `DEMO_VISUAL_DEBUG` 6.4). Resultados con la instancia sana: el eval canonico describe la sala con "efecto de pixelacion o malla visible en los planos" (la mezcla se lee como textura), suelo/techo/paredes correctos, contornos limpios; y el **pase C** (elementos del INTENT + la mezcla): todos PRESENTES y coherentes, "sin superficies partidas, invertidas ni triangulos", y la textura de puntos "**uniforme y regular... sin ruido, distorsion ni variacion irregular**", con los paneles solidos contrastando. Respuesta cruda completa: `080_camera_pan_window_report.md`.
