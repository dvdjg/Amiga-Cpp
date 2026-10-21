# VALIDATION — 080_camera_pan_window (F1: paneo de cámara con frames intermedios)

Estado: **ABIERTA / NO VERIFICADA** por cadencia base inferior al objetivo F1 de 12,5 Hz y por falta de veredicto de flicker. El usuario acepta provisionalmente la cadencia actual y pausa su optimización. Yaw, pitch y Lissajous pasan secuencias por paso de 32 frames; una captura de integración de 260 frames cubre las fases y los extremos. La base sigue bajo el objetivo y la comprobación de flicker/tearing continúa pendiente.

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

## 2.1 Publicación al inicio de campo y contrato adaptativo (2026-10-18)

El buffer del Full se **publica en el update siguiente** (que corre tras el VBlank, antes de que el haz llegue a los `MOVE` de punteros): así el frame presentado es entero y desaparece el corte a mitad de campo que salía al parchear nada más terminar el render (que desborda el campo, DT-034). Latencia de 1 frame en la publicación (como la pipeline de la 116); el offset de display del propio Full es el paneo avanzado desde su render (0..15). Verificado con captura por paso (`--sequence-step-frames 8`): frames **limpios** (visto por el agente) y **verificador adaptativo con invariantes OK y 4/4 pares de contrato** (fake == Full publicado + delta de offsets; costura 1088-1354 px ≈ 2 %). El chequeo de "Fulls consecutivos" es **robusto a la fase de captura** (el parche se aplica al campo siguiente y el desfase efectivo varia con la cadencia): busca el desplazamiento dominante en k=1..6 y exige minimo claro con costura acotada. Con 12 frames por paso, la secuencia pasa **completa** (`OK: 080 paneo`).

## 2. Contrato en la secuencia Full/Fake

```bash
bash tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --sequence-step-frames 16
node tools/analyze/verify-080-pan-window.mjs --sequence-dir out/run/080_camera_pan_window/A500_debug/sequence
```

Salida (resumen del 2026-10-18): telemetría de cada frame == reloj esperado (Full/Fake y offset exactos), 3 pares de contrato con desplazamiento dominante k=1 (0-3 pares exactos según la fase de captura; el resto con la costura de captura ≤ 2 200 px / 3,3 %), y Fulls consecutivos con desplazamiento dominante k=2 (la escena avanza con la cámara). La costura de captura es la firma del **frame que desborda el campo** (el parche del siguiente frame cae a mitad del campo visible): es la evidencia del hueco de cadencia (§5). Evidencia del build con **política fija** (racha 1): con la política adaptativa (`K` por coste medido) el verificador `verify-080-pan-window.mjs` ya no replica la planificación, solo **invariantes** (offsets 1..15, `pan == lastFullPan + off`, rachas ≤4, Full al menos cada 12 frames) y el contrato por el **offset reportado** en la telemetría; recapturar esa secuencia está bloqueado por la captura por paso (DT-033).

## 3. Visión (Ollama, regla de oro)

- `src/VISION.md` (prompt canónico **actual**, que mira contornos; `vision-final.mjs`, 5 frames de la captura por intervalo del build con patrón): el modelo describe el fondo como un patrón de bandas geométricas y **barras verticales que se desplazan** entre imágenes (con la reaparición de una columna desde el margen — el desplazamiento de ventana) y el HUD `FULL 12.5 + FAKE 25` con `K 2`; concluye **contornos limpios, sin halos ni restos de la posición anterior**, con el fondo uniforme. La reevaluación **con captura por paso** (frames nítidos, sin el banding del Full mid-render) sigue pendiente: la captura por paso está bloqueada (DT-033).
- `ollama-frames.mjs` sobre una secuencia: confirma **movimiento apreciable** ("la escena se ha movido hacia la izquierda, revelando partes nuevas") — la dinámica del paneo es observable por el modelo.
- **Nota (2026-10-19)**: el eval no detectó la **inversión de la sala a pan > 0** (DT-043): preguntado explícitamente por la orientación del suelo en una captura congelada a +13 (suelo marrón arriba), respondió que el suelo está abajo y la escena es coherente. El pase C contra los elementos del `INTENT.md` es el que debe marcarlo; el eval libre no es veredicto.

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
node tools/debug/profile.mjs 080_camera_pan_window A500_debug 5 \
  --names Frame,Full,Fill,Patch,Hud,HudCtor,HudLine1,HudLine2,Wait,Clip
```

`profile.mjs` imprime por defecto los nombres de sección de otra demo (086): sin `--names` la tabla se lee mal. Si `Fill` sale 0 con la escena visible, el `.exe` es de una ablación (`K080_SKIP_FILL`) o está viejo: recompilar limpio antes de medir.

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
- **No cubierto / SIN VERIFICAR**: **base ≥12,5 Hz** (full ≈3,4 campos, hace falta ≤3,0; DT-034); ausencia de tearing mientras el frame desborde; cobertura amplia de pitch (F3 del roadmap), culling general para pitch y horizonte Copper (F4), fuera del alcance de esta demo.

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

## 10. Cuelgue intermitente (~50% de arranques): espera de VBlank bajo el SO (2026-10-19)

**Sintoma**: con el build correcto y estable en la mayoria de arranques, ~40-50% de las corridas clavaban el frame en un punto variable (7/52/100/350) con la tarea dormida (PC en el `STOP` del dispatcher de exec, `INTENAR` con la mascara por defecto del SO `0x7600` sin el bit del latido del engine, `INTREQR` con el bit pendiente sin atender). Diagnostico en vivo por canal lateral (MCP): el update n-esimo completo (prof `frames` = frame+1) y el bucle quedo esperando un latido que ya no llegaba.

**Causa (familia 212/DT-011, ya documentada)**: bajo un SO vivo el planificador/ROM reescribe `INTENA` en los cambios de tarea apagando `VERTB`; con el camino `Wait()` por defecto la tarea se duerme para siempre. La 080 no se habia migrado al modo estable. Con frames largos (el Full ~3.4 campos) el clobber es casi continuo: la espera activa sola peleaba contra el y **arrastraba** (~4 s/frame).

**Arreglo**: `run_frames_polling` (bucle por SONDO de `VPOSR`, sin depender de IRQs ni de `Wait`) — el modo estable del doc 212 y el de la 119. Verificacion: **6/6 arranques vivos** (frames 8-9 -> 91-93) y corrida larga con **frame 359+ y 40/40 frames distintos**; verificador adaptativo en verde (**`OK: 080 paneo (contenido/HUD/contrato de pixel)`**, pares de contrato con la costura historica, fulls consecutivos coherentes) con captura por paso; vision canonica sobre la secuencia larga: pan descrito con precision y elementos del INTENT presentes. La banda rasgada ocasional junto al borde del suelo es **el tear del frame que desborda el campo** (3.4 campos > 1; criterio 3 del INTENT): se cerrara con la optimizacion de F1 (DT-034), no con un parche de dibujo.

**Guarda de configuracion (engine, 2026-10-19)**: para que este fallo no vuelva en silencio, el engine gana `AmigaBackend::vblank_wait_unsafe()` (espera por `Wait()` + proceso bajo SO + sin takeover) y `Engine::run_frames` la comprueba AL ENTRAR: si se cumple, marca `FAILED` con codigo propio `0xE212` y NO entra al bucle. Verificado con la 080 volteada temporalmente a `run_frames`: fallo ruidoso y determinista 3/3 (`detail=0xe212`) en vez del cuelgue intermitente. El bucle por sondeo (`run_frames_polling`) y el modo con takeover no disparan la guarda.

**Estabilidad del damero (2026-10-19)**: con `run_frames_polling` el modo `-DK080_PATTERN` es estable de nuevo: 4/4 arranques vivos (frames 7-9 -> 89-90). Su inestabilidad era enteramente la espera de VBlank (familia 212).

**Full tras el solido (2026-10-19)**: ultimo perfil valido del build con render correcto: `Full` ~562k ciclos (~3,9 campos) media -> K=2 -> base ~9 Hz: **DT-034 sigue ABIERTA** (el damero ya no pesa, pero el relleno por plano base domina); el culling y la sala solida dejan la campana de F1 sobre una base sana.

## 11. F2: pasada completa rebanada por etapas (2026-10-19) - EN CURSO

**Diseño**: el Full se divide en **clip → relleno de sala (plano 0) → relleno de sala (plano 1) → relleno de paneles**, una etapa por update, con la **publicación diferida** al inicio de campo. Mientras hay una pasada en curso no se consulta al `PresentController`: se presenta el Full anterior desplazado (fakes) y cada update avanza una etapa. En la fase rápida (16 px/update) se encadenan todas las etapas en el mismo update (no hay margen para interpolar). El reparto por **plano** (no por filas ni columnas) evita costuras: el area fill ve exactamente el mismo contorno y la misma paridad que la llamada de 2 planos.

**Equivalencia de píxel (captura congelada, `-DK080_PAN_FREEZE`)**: el build rebanado es **pixel-idéntico a HEAD** (`cb5eaaaf`, `render_full` monolítico) a pan 0, −13 y +13 (diff 0 px en la escena; solo cambian los contadores del HUD). La comparación se hizo con un worktree temporal de HEAD y `scenediff.py`/`diffmap.py` (out/tmp).

**Cadencia medida (build canónico, `profile.mjs` con `--names`, 5 s)**: 179 frames, **199.514 ciclos/frame = 1,41 campos/update** (≈35 updates/s); sonda del runner: 113 updates en 3,52 s = **32,1 updates/s**. Los rellenos por plano miden **114-180k** por llamada (una llamada puede pasar de 1 campo), `clip` 69-118k, parche ~1k. La publicación (Full real) sale cada ~4 updates → **base ≈ 9 Hz**: el rebanado **no reduce el trabajo total** del Full (~4 campos), solo reparte el coste y mantiene la pantalla viva (display **32-36 Hz**, antes ~24,6). Cerrar la base de F1 exige **solapar** el Blitter con CPU (cola IRQ de F2) o abaratar el relleno (DT-034), no más etapas.

**Mejora visual verificada**: con publicación diferida + etapas desaparece el **parche a mitad de campo** que en HEAD dejaba una columna de basura en el borde izquierdo (visible en la captura congelada de HEAD a +13: barra rojiza en x≈76). El build rebanado no la tiene.

**Hallazgo (preexistente, registrado como DT-043)**: a **pan > 0** la sala se renderiza invertida (suelo marrón arriba, techo azul abajo; paneles correctos). Está **también en HEAD** (misma imagen, diff 0 px): no es del rebanado. El modelo de visión consultado (`qwen3-vl:8b`) **niega el defecto** («el suelo está en la parte inferior, la escena es coherente») — nueva evidencia de que el eval sin contraste no es veredicto.

**Bisección DT-043 (2026-10-19)**: con `-DK080_CPU_FILL` (mismo conjunto de caras y mismo clip, relleno CPU) el +13 sale **correcto** → la geometría y el clip están bien; el defecto está en el **camino Blitter** y solo en el **plano 1**. Descartados por A/B el **dedupe de aristas** (desactivarlo da la misma imagen rota) y los **vértices duplicados** (VER=40, ninguno). El volcado de la chip RAM por canal lateral no es fiable aquí (la zona de la escena devuelve ceros; el programa sí se lee): para el siguiente paso, usar memwatch/GDB o un HOST que replique el trazador ONEDOT + area fill.

**Medida vigente (2026-10-19, build solido + culling + `run_frames_polling`, flags certificados)**: HUD tras ~3 s de corrida a intervalos (`--sequence-frames 12 --sequence-interval-ms 300`): `FRAME 64 / FULL 24 / FAKE 41 / FULL OFF 1 / FAC 9 / VER 40 / PAN -19 / K 2`; perfil (`profile.mjs`, 5 s): **124 frames, 287.642 ciclos/frame = 2,03 campos por update** (~24,6 updates/s). Con `RenderPacing`, K=2 implica `full_fields = 4` (≈567k ciclos, coherente con los ~562k de arriba): **base ≈ 8,2-8,3 Hz**, display ≈ 24,6 Hz. El reparto de la ventana (24 Full / 41 Fake) confirma el ciclo Full+2 Fakes. DT-034 sigue abierta; la palanca es el relleno (solapar/rebanar, F2).

## 12. Reanudación: yaw, culling y captura por paso (2026-10-20–21)

Build validado con el `build.args` certificado (`-O3` + `-fno-tree-vectorize`). En capturas estáticas a `K080_PAN_FREEZE=13` y `65`, la habitación mantiene el suelo abajo, el techo arriba y los cuatro paneles visibles; en ambos ángulos `freeze-diff.mjs` compara culling normal y `K080_NO_CULL` con **MAD=0 y cero píxeles distintos**. `verify-080-pan-window.mjs --image` pasa presencia de panel único, fondo y HUD para las dos poses.

La captura por paso volvió a funcionar en una secuencia de **128 updates consecutivos**, frames f0026–f0153, que recorre yaw negativo y positivo (hasta pan +70 en el tramo medido). `verify-080-pan-window.mjs --sequence-dir` pasa 128/128 frames, 95 pares Full/Fake dentro de la tolerancia de captura, todos los offsets dentro de margen, Full al menos cada 12 frames y racha máxima de tres Fakes; 31/31 Fulls consecutivos tienen desplazamiento dominante coherente. Una secuencia más larga muestreada por intervalo cambió en 31/31 pares. Después de extender `PresentController` a dos ejes, el build yaw se reconstruyó y una captura estabilizada (`--settle-ms 3000 --sequence-step-frames 32`) pasa 32/32 frames, 14 pares Full/Fake comparables y 7/7 pares Full consecutivos; 30/31 imágenes consecutivas cambian. Ollama volvió a describir la secuencia como estática aunque PAN cambia de −19 a +29; `src/VISION.md` conserva esa respuesta cruda. No extrapolar esta secuencia de 32 frames a todo el recorrido ±128.

El resultado despeja el bloqueo de culling Full/Fake para yaw en las dos poses congeladas y en la trayectoria capturada, pero no valida todas las poses ±128. La cadencia de Full sigue bajo el objetivo original; por instrucción del usuario se acepta provisionalmente el FPS actual y se pausa la campaña de rendimiento.

## 13. Barridos puros yaw/pitch de 120° y límites del frustum (2026-10-21)

El framebuffer mide 352×256, el viewport 320×208 y las guardas 16 px X/24 filas Y. La sala llega a `z=32`, por lo que el suelo y el techo intersectan el campo incluso con la cámara a ±60°. El techo verde se distingue del fondo y las paredes azules; se mantuvo sólido porque la teselación 4×4 introducía líneas discontinuas en los bordes compartidos. `K080_PAN_FREEZE=±268` fija ±60°: en yaw se ve una pared lateral que entra y sale del campo; en pitch el suelo y el techo ocupan sucesivamente el viewport. El culling yaw-only se aplica solo para `|pan_x|≤65` (≈14,5°), donde se validó A/B; para giros mayores y cualquier pitch se usa clipping general. El panel único puede quedar fuera del viewport en los extremos, que es el resultado esperado del barrido.

Tras estos cambios, los builds puros default-yaw y `K080_PITCH_TEST=1` compilaron y pasaron secuencias de `--settle-ms 500 --sequence-step-frames 32`: `verify-080-pan-window --axis x` (yaw) y `--axis y` (pitch) pasan 32/32 frames, márgenes/pacing y 15–16 pares Fake comparables; los pares Full consecutivos muestran desplazamiento coherente. El diff detecta cambios en los 31/31 pares consecutivos. El primer Fake sirve de referencia cuando la captura del Full precede al parche Copper efectivo. Las poses congeladas ±60° muestran la escena dentro/fuera del frustum; la cobertura por paso de 32 frames no sustituye el ciclo integrado.

## 14. Trayectoria Lissajous y ciclo de integración (2026-10-21)

`K080_LISSAJOUS_TEST=1` usa yaw `sin(φ)` y pitch `sin(2φ)`, amplitud de ±40° y la tabla seno Q12 sin trigonometría runtime. En 32 frames, `verify-080-pan-window --axis xy` pasa invariantes y 15 pares Fake comparables con búsqueda del delta vertical (7 exactos; máximo 738/65.714 píxeles); 31/31 pares consecutivos cambian. El análisis bidimensional usa el primer Fake como ancla por la fase de publicación y busca `dy` dentro de la guarda Y.

La ejecución predeterminada (`--sequence-frames 260 --sequence-interval-ms 300`) recorrió yaw, pitch, Lissajous y el retorno al inicio: 260 capturas, 256 pares cambiados y 3 duplicados detectados. La visión canónica reconoce los cambios de perspectiva, la desaparición/reentrada de columnas y los colores diferentes del techo/fondo; su OCR del HUD y algunos sentidos de giro son imprecisos, por lo que se contrastó con la inspección de imágenes y el diff objetivo. La secuencia dinámica confirma los tres modos; el contrato Full/Fake se valida por paso y por eje con las compilaciones de prueba.

Prompt dirigido exacto (frames 0, 100, 200 y 259): `Compara estas cuatro imágenes en orden. Describe qué cambia en la perspectiva, en la posición de las cuatro columnas y en qué superficies de color son visibles. ¿Se aprecian fases distintas de rotación combinada? Indica si hay superficies cortadas o huecos en el viewport y comenta cualquier cambio en el HUD; no adivines valores numéricos que no se lean.` Respuesta cruda (extracto): `Las columnas se mueven horizontalmente en la pantalla […] En las imágenes 3 y 4, la columna naranja está parcial o completamente cortada por el borde del viewport. Las superficies principales (techo, paredes, suelo) permanecen visibles […] No hay bandas, halos ni píxeles cortados.` El modelo confirmó la entrada/salida, pero inventó una quinta columna y leyó mal el HUD; prevalecen el conteo de cuatro paneles en los frames y los datos de movimiento del programa.

La aceptación de rendimiento de F1 sigue siendo provisional (Full ~8–9 Hz frente a 12,5 Hz) y el flicker/tearing no se ha medido en esta fase. No declarar la demo VERIFICADA hasta cerrar esos gates.

## 15. Clipping near en yaw moderado (2026-10-21)

Se corrigió el fast path de `stage_clip`: el umbral angular del clipping general no garantizaba que cada cara siguiera delante del plano near. La pared cercana empieza en `z=32`; con yaw +65 (≈14,5°), algunos vértices pasan detrás de `near=4`, aunque el yaw siga dentro del antiguo rango de proyección directa. Proyectarlos antes de recortar invertía el signo de `z` y producía triángulos grandes espurios. Ahora cada cara que tiene un vértice por detrás de near se recorta en espacio de cámara antes de dividir; las caras completamente delante conservan el camino rápido y el recorte 2D de laterales.

Regresión: HOST-481 cubre `camera_vertices_at_or_beyond_near` con vértices sobre/delante de near, y una cara que lo cruza (incluido el caso de yaw moderado de la pared cercana). La captura WinUAE congelada a yaw ±65 muestra la sala completa sin los grandes triángulos invertidos de la captura previa; culling encendido y `K080_NO_CULL=1` son **bit-exactos** en ambos sentidos (MAD=0, cero píxeles distintos). Ollama no detecta triángulos espurios ni geometría deformada. El verificador de imagen cuenta los cuatro colores base de la sala, no solo el fondo `0x124`, que puede quedar casi cubierto en una vista oblicua.

## 16. Caja de Fake, velocidad y limpieza de planos (2026-10-21)

El límite Fake es un presupuesto de desplazamiento acumulado desde el último Full, no 16 px por frame. La guarda física horizontal es de 16 px, pero los offsets seguros son ±15 porque `S` va de 1 a 31 alrededor de 16. El verificador acepta Fake de offset cero (movimiento subpíxel/redondeado a cero) y ahora distingue el tag `A3`: el controlador ya pidió un Full por agotamiento de margen/cap, mientras la imagen vieja se muestra clampada; el `A1` siguiente confirma la publicación. La secuencia `K080_LISSAJOUS_PHASE_STEP=18`, 32 frames, provoca repetidamente el caso 16–20 px: se solicita Full con el offset viejo clampado a 15, nunca se sale de la guarda, racha máxima 3 y los pares de Fake pasan el diff 2D. Las fases compuestas 3 y 9 también pasan `verify-080-pan-window --axis xy`; el paso 9 alcanza offsets 14 y el Full se fuerza antes de exceder el margen. El criterio es el offset medido, no el número nominal de Fakes.

La secuencia larga mostró residuos verticales de los colores de las caras al mover el frustum. Se quitó el clear por bounding box para los pases de sala/paneles y cada Full vuelve a limpiar el plano completo antes del EOR/area-fill; la evidencia de WinUAE de 260 capturas en la trayectoria, junto con los frames inspeccionados y Ollama, ya no muestra trazos aislados ni restos de la posición anterior. El recorrido captura yaw, pitch intermedio y Lissajous; las vistas dominadas por una sola pared/suelo/techo son el frustum saliendo de la sala, no una pérdida de planos.

## 17. Cubos compuestos y oclusión (2026-10-21)

Dos cubos de lado 56 orbitan en sentidos opuestos alrededor de `(0,0,480)` (radios X=100/Z=80) y usan spins Euler diferentes. Cada una de las seis caras de cada cubo tiene un índice RGB distinto: A usa 4–9, B 10–15; los paneles escriben los índices completos 4, 8 y 12, que conservan sus colores identificadores. La proyección recorta cada cara en cámara y viewport; el culling usa normal·vector-a-cámara en s32 y el painter sort dibuja las caras lejos→cerca. Las cajas y paneles escriben los cuatro planos para conservar un índice de color opaco completo, mientras que la sala mantiene sus dos planos base.

La sonda HOST-481 compara el culling general float contra `math3d::face_signed_area` en yaw simétrico y giro compuesto; su fixture Fixed Q0 contrasta literalmente el producto `mul_wide` s16×s16→s32 usado por el target y comprueba frente z−/reverso z+. `K080_CUBE_PROBE=1` centra una sola caja; con `K080_CUBE_FREEZE=1`, `K080_CUBE_START_PHASE=256`, `K080_PAN_FREEZE=0` y `K080_SKIP_FILL`, ocho capturas estáticas muestran tres caras de colores distintos (verde, amarillo y cian) con bordes conectados; Ollama no detecta huecos ni caras atravesadas. La pose estática de dos cajas `K080_CUBE_START_PHASE=1280` produce dos sólidos distinguibles y en contacto, con caras convexas y sin fragmentos, según la captura y Ollama.

La animación aislada de las cajas, con cámara congelada y el resto de la escena omitido (`K080_PAN_FREEZE=0 -DK080_SKIP_FILL`), se capturó durante 12 frames a intervalos de 700 ms: ambas cambian orientación/posición, conservan caras opacas y no dejan restos de posiciones previas. La primera integración reveló que el buffer alterno conservaba los bits de color de cajas/paneles del Full anterior; `stage_panels` limpia los planos 2–3 completos antes de dibujar los índices de cuatro planos. Después de ese clear, el recorrido integrado predeterminado se volvió a capturar en 260 frames a intervalos de 300 ms: `analyze-frame-sequence.sh --expect-animated` informa 257 pares cambiados y 2 duplicados de muestreo. Ollama en los frames 0, 86, 173 y 259 describe caras conectadas y no informa bandas ni restos; en yaw/pitch extremos las cajas entran/salen del viewport, por lo que pueden aparecer recortadas por el borde. La respuesta dirigida de frame 173 interpreta el solapamiento como dos sólidos opacos en contacto, sin huecos ni fragmentos.

El contrato Full/Fake se comprobó además con 16 capturas por paso (`verify-080-pan-window --axis x`): 16/16 frames válidos, cuatro Fulls, doce Fakes, racha máxima tres y siete pares con desplazamiento de ventana coherente. La captura por intervalo no alinea la telemetría por frame y el verificador la rechaza para ese contrato. Perfil de 48 frames del build integrado: 743.075 ciclos/frame de media (5,24 campos), Full medio 623.581 ciclos/frame, hasta 2.775.442 ciclos por llamada Full, HUD `F=14, K=3`; la cadencia base sigue pendiente/aceptada provisionalmente y no se optimiza en esta pasada.
