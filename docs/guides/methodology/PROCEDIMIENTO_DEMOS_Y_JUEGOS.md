# Procedimiento: crear y arreglar demos y juegos (de la intención al cierre verificado)

Este documento es la **puerta de entrada** del procedimiento para **generar y arreglar demos y juegos** sin que se cuele basura. Define las fases, las puertas (gates) que hay que pasar, el presupuesto de la máquina y qué hacer cuando algo falla — **incluido lo que falla y no es de tu turno**. Los detalles de cada parte viven en sus documentos canónicos (abajo); aquí está el flujo y las reglas duras.

## 0. Documentos canónicos del procedimiento

| Documento | Qué cubre |
|---|---|
| [`development-methodology.md`](development-methodology.md) | Método incremental general: fases verificables, no big-bang. |
| [`PROTOCOLO_ETAPAS_GRAFICOS.md`](PROTOCOLO_ETAPAS_GRAFICOS.md) | Construcción por etapas de gráficos/hardware (CPU primero; parar tras 3–4 intentos). |
| [`DEMO_VISUAL_DEBUG.md`](DEMO_VISUAL_DEBUG.md) | Demos atractivas, captura de secuencias y **protocolo de prompts de visión** (§6.4). |
| [`docs/build/BUILD_AND_RUN.md`](../../build/BUILD_AND_RUN.md) | Compilar, ejecutar, capturar y depurar (build → run → analyze). |
| [`docs/testing/README.md`](../../testing/README.md) | Toda API con test; sin demo, la feature queda NO VERIFICADA. |
| [`docs/tools/PROFILING_FROM_AGENT.md`](../../tools/PROFILING_FROM_AGENT.md) | Medir coste por frame (`.amigaprofile` + IA local). |
| [`ZERO_COST_FRAME_PATH.md`](../../engine/architecture/ZERO_COST_FRAME_PATH.md) | El bucle de frame no hace trabajo evitable. |
| [`ROADMAP_DEUDA_TECNICA.md`](../roadmap/ROADMAP_DEUDA_TECNICA.md) | Registro de deuda y de demos **NO VERIFICADAS** (incluye errores ajenos al turno). |
| `AGENTS.md` §3 | Objetivos de rendimiento (50 fps PAL, sin tirones) y operación. |

## 1. Presupuesto de la máquina (no negociable)

- **Una demo Amiga completa un frame de juego por VBlank PAL (~50 Hz).** Un efecto de relleno de polígonos puede bajar a 25 fps; el resto, no.
- **Excepciones: documentadas y con suelo.** Una demo intrínsecamente cargante (relleno masivo por píxel, física pesada, streaming) puede declarar un objetivo menor —**nunca por debajo de 25 fps**— y debe quedar escrito en su `VALIDATION.md` con el motivo, el coste real por frame y la medida. Sin documentar, la excepción no existe y la demo queda NO VERIFICADA.
- El **frame debe caber en 1 field (~141,9k ciclos de CPU)**; si ocupa 2+ fields, la animación va a tirones y cualquier escritura durante el barrido se ve (flicker/tearing). Medido con `tools/debug/measure-fps.mjs` (imprime `fieldsPerFrame`).
- **Presupuesto por elemento del bucle principal.** Antes de dar una demo por buena hay que **desglosar el coste de cada elemento** del bucle (scroll, composición, objetos/FG, IA, audio, telemetría…) **y en especial las rutas que no se ejecutan siempre** —spawn, recarga/wrap del scroll, cambio de fase, primer frame— que son las causantes habituales de los **tirones**. Se anota el **peor caso**, no solo la media; el perfil (`docs/tools/PROFILING_FROM_AGENT.md`) confirma el desglose y un pico repetido invalida la demo hasta replantear la técnica.
- **Trabajo de runtime mínimo**: se resuelve en compilación lo que se pueda; nada de `memset`/`memcpy`/construcción de objetos por frame (ver `ZERO_COST_FRAME_PATH.md`).
- **Descartar `Finite`/modos de borde si una decisión de arquitectura ya fijó otra política** (p. ej. las *Decisiones tomadas en 202*: scroll toroidal único, sin modos de borde finito). Consultar el roadmap del dominio antes de elegir modos.

## 2. Fases y puertas

```
F0 Diseño -> F1 Esqueleto -> F2 Implementación por etapas -> F3 Gates técnicos
   -> F4 Validación visual (visión + ojo del agente) -> F5 Cierre y registro
```

- **F0 · Diseño.** Escribir la **intención**: qué se pretende ver y un **criterio de éxito visual**. Definir los **elementos esperados** (cuántos y de qué color/forma; al menos un elemento de color único con trayectoria observable). Presupuesto de ciclos estimado.
- **F1 · Esqueleto.** `build → run → analyze` en verde desde el primer commit. Telemetría en `g_eng_run_status.detail` (p. ej. cámara) y su `analyze-sequence.sh` propio con `--expect-animated`. Si el arranque/display falla, no se sigue.
- **F2 · Implementación por etapas.** Respetar `PROTOCOLO_ETAPAS_GRAFICOS.md` (CPU como referencia primero; parar tras 3–4 intentos). Cada etapa deja el frame **en presupuesto**; no se acumula deuda de rendimiento para "arreglarla al final".
- **F3 · Gates técnicos** (todos, cada uno con su salida como evidencia):
  1. `bash tools/test-regression.sh --demo <ruta>` (build/run/analyze + contrato de movimiento).
  2. `node tools/debug/measure-fps.mjs <demo> <config>` → **50 fps / 1 field** (o 25 fps si el efecto es relleno); sin picos de ciclos.
  3. `bash tools/test-regression.sh --flicker --require-flicker-ok --demo <ruta>` → 0 candidatos; **declarar `flicker-baseline.json` solo después de F4**.
  4. Tests host del dominio (`docs/testing/README.md`) y gates globales (`tools/run-host-tests.sh`).
  5. **Presupuesto por elemento y peor caso**: desglose por elemento del bucle (incluidas las rutas no frecuentes que causan tirones) con la medida que lo respalda. Para atribuir coste cuando el perfil por secciones no esté disponible: **ablación** (desactivar un elemento, medir y comparar).
  6. **Presencia de elementos**: `node tools/analyze/check-elements.mjs <seq> <colores> --expect <color>=<min>` — falla si un elemento esperado desaparece o queda a medias en algún frame.
- **F4 · Validación visual** (obligatoria; no sustituible por F3):
  - Capturar **secuencia determinista por paso de frame**: `run-demo.sh <demo> --sequence-step-frames N` (un frame de juego entre capturas, `eng_debug_ready_probe`); el modo por intervalo (`--sequence-frames/--sequence-interval-ms`) solo para vistazos. Cubrir las **transiciones internas** (arranque, rebotes, envolturas, cambios de fase).
  - Aplicar el **protocolo de prompts** de `DEMO_VISUAL_DEBUG.md` §6.4: (A) **inventario sin contexto**, (B) **dinámica/seguimiento**, (C) **verificación dirigida** con los elementos esperados.
  - **El agente mira los frames** y aporta una **comprobación objetiva de presencia** (conteo por color/región, `band-diff`, MD5/diff de frames). **Una anomalía de contenido es bloqueante** hasta confirmarla o refutarla con inspección propia y evidencia, nunca con un gate de movimiento.
  - El veredicto y la respuesta cruda del modelo se guardan como evidencia.
- **F5 · Cierre y registro.** README de la demo (efecto, técnica, contrato) y **`VALIDATION.md` en la carpeta de la demo** con: prompts exactos enviados a visión, **respuestas crudas**, medidas (fps/ciclos, `check-elements`, `band-diff`), conclusiones y deuda abierta (DT). **Sin F3+F4 en verde la demo queda NO VERIFICADA** y así se dice en el informe.

## 3. Reglas duras (anti-basura)

1. **Ninguna demo se declara verificada sin F3 y F4 en verde y con evidencia reproducible.** Los gates de cobertura/tonos/movimiento **no** validan contenido.
2. **Nunca una captura única**; siempre secuencia. Un frame "bueno" no prueba nada.
3. **Un aviso de visión sobre contenido es bloqueante:** se confirma o refuta mirando los frames y con conteo objetivo. Prohibido pedir coordenadas en píxeles al modelo.
4. **Nada de pintar con borrado/repintado sobre un lienzo visible single-buffer**: o se pinta dentro del VBlank con margen, o se dobla el buffer. Un hueco de borrado mayor que el barrido de la zona = flicker visible. Y **no basta con pintar pronto: el orden importa**. Con capas por bandas, pinta **de arriba abajo**: la capa viva del juego (sus blits) primero y la franja del HUD después (`HUD_SYSTEM.md` §9.3). Con el HUD primero, el trabajo CPU del HUD retrasa la ejecución del plan hacia la mitad del campo y el haz ve los blits a medias (medido en la 302: parpadeo en 18 de 40 frames; con el orden correcto, 40/40).
5. **Actualizar por señal de VBlank** (`eng::os` + `run_frames`), no por sondeo, salvo **fallback justificado**: un **juego que corre como proceso AmigaDOS sin takeover** (lanzado desde el Shell, con el SO vivo) usa `App::run_polling` (`run_frames_polling`): la ROM reescribe INTENA a los ~1,5-2 s y apaga VERTB, y el camino de `Wait()` puede morir con el frame congelado (ficha [`212-vblank-irq-apagada-por-el-so.md`](../../debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md); resuelto así en `games/400_rpg_slice`). Las demos con takeover (`Forbid()` + `set_vblank_sleep(false)` + restauración, receta en el punto 20) mantienen el latido por IRQ.
6. **Cada rojo de un gate se registra** en `ROADMAP_DEUDA_TECNICA.md` con síntoma + evidencia (aunque no sea tu tarea y aunque sea previo). No se "sigue adelante" con un rojo sin registrar.
7. **No maquillar la medida**: no relajar umbrales, no alterar telemetría ni cadencia para simular cumplimiento.
8. **Excepciones de presupuesto solo documentadas** (nunca por debajo de 25 fps) con motivo y medida en el `VALIDATION.md`.
9. **Nada de "ya se optimizará después"**: cada etapa deja el frame en presupuesto, con el peor caso medido (incluidas las rutas que no se ejecutan siempre).
10. **Cada demo tiene su informe de validación** (`VALIDATION.md`): prompts de visión + respuestas + medidas + conclusiones; se actualiza en cada pasada que cambie su render o su coste.
11. **Usar el modelo de visión local (Ollama) sin límite** para todo lo que aporte (inventario, dinámica, dirigido, dudas sobre frames): es local y gratis; ante la duda, otra pregunta y otra pasada.
12. **El alcance de la afirmación no puede exceder el de la prueba** (AGENTS §1.5): enumerar los grados de libertad (planos, ejes, buffers, regiones, fases, caminos) y decir qué cubre la medida y qué no; lo no cubierto queda **SIN VERIFICAR**. Un test que solo mira una franja/registro no autoriza un «está bien» global; se informa «correcto en el alcance probado (…), sin verificar (…)».
13. **Diagnóstico por aislamiento antes de arreglar**: reproducir con el mínimo de elementos (p. ej. scroll sin objetos), con datos **identificables** (el propio contenido codifica su índice), expectativa escrita por frame y comparación objetiva (copper dump/punteros, `band-diff`, conteo). No se itera lanzando la escena completa «a ver si mejora».
14. **La herramienta de observación es parte del sistema**: si mide mal (captura, perfilador), se arregla primero; no se sustituye por parches ad-hoc (scripts temporales) que quedan fuera del procedimiento.
15. **Medir los fps EN CUANTO la escena dibuja** (no al final): `tools/debug/measure-fps.mjs <demo> <config> --json` — o, si esa herramienta falla en el entorno, la ventana `eng::debug::HudFpsWindow` visible en la franja de depuración (leída con visión) y la ventana del perfilador. Si no da ~50 fps, el sospechoso nº 1 es el **número de lanzamientos del Blitter** (~2-5k ciclos por job; medido en la 066; no solo los words bliteados). Presupuestar los jobs como recurso de primera clase: los **rellenos pequeños** (pips 8×8, deltas de medidor, marcos de 1 px) **no** deben ir por Blitter aunque el `AccelMode::Auto` los mande allí por área ≥ 64 px — el arranque domina (~10-14k por rect); `HudStrip` fija `AccelMode::Cpu` en su lienzo (302: `apply` de ~208k a ~18k, 26 → 49,9 fps). El **camino async es viable bajo un SO vivo** (la 066 lo usa a 37.5 fps; el fallo «la ROM apaga la IRQ BLIT» era una hipótesis falsa — la causa real fueron los bugs de caché/feeder/gate/servicio, cerrados en `docs/debugging/investigaciones/066-async-blit-irq-bajo-el-so.md`). Ojo: el **lanzamiento** programa el primer job (y espera entre planos de un job multi-plano); con cadenas largas eso serializa el frame — medirlo por secciones (`ENG_PROF_*` + `g_eng_prof`, leído por canal lateral o por contadores del periférico de depuración) antes de decidir.
16. **Las hojas de BOB en la arena NO vienen a cero**: limpiar la hoja completa antes de escribir (si no, los planos altos conservan basura y los bobs salen con colores/valores imposibles). La matriz de render por píxeles exactos está en HOST-439 (`tests/host/graphics/439_bob_render_matrix`): úsala como referencia antes de culpar al consumidor.
17. **WAITs del Copper: estrictamente crecientes en la lista** (un WAIT hacia atrás se cumple al instante y colapsa todo lo que venga después: bandas de color en ráfaga, sprites a destiempo). Cuidado al intercalar **rearmes de multiplexado de sprites** (líneas del VSTART) con bandas de color: o todo el listado respeta el orden, o las bandas colapsan. Y el **armado de un sprite debe ir ANTES de su VSTART** en el frame (si no, el DMA no lo ve hasta el siguiente).
18. **Versionado obligatorio** (norma 2026-10): toda demo lleva `major.minor.build` (`eng/debug/version.hpp`; el build lo inyecta `build-demo.sh` con contador en `out/.build-number`) y **reserva la franja inferior** para nombre + versión + depuración (contador de frames/fps). El HUD de depuración debe ser **lo más sencillo posible**: un **único bitplane** con texto impreso **por palabras** (`Surface::draw_text_words`, dos glifos por palabra; nunca píxel a píxel). Referencia: `draw_hud` de la 120; `eng::debug::VersionStrip` para franjas preexistentes multi-plano (065/066). Un HUD que repinte lienzos completos cada segundo puede costar ~22 campos: medirlo con `measure-fps` y ablación (`K120_SKIP_HUD`).
19. **El Blitter es un recurso compartido con el SO**: cualquier código (servidores del SO incluidos) puede programar sus registros entre dos jobs; la caché de registros comunes del backend se invalida **por job** (bug real de planos perdidos en la 066). Con el camino **async**, el servicio del `wait_blitter` es el propio feeder: los jobs multi-plano se re-entran (guard `g_in_blit_service`). Antes de culpar a la demo, comparar sync vs async y leer `docs/debugging/investigaciones/066-async-blit-irq-bajo-el-so.md`.
20. **Control de la máquina**: bajo el runner (AmigaDOS vivo) el setup de demo es `Forbid()` + `set_vblank_sleep(false)` + takeover; el procedimiento canónico completo (entrada y **vuelta al SO**) está en `docs/reference/amiga/hardware/takeover-y-restauracion-canonica.md`. Si algo "no cuadra" con IRQs/INTENA, leer ese doc y la 212 antes de experimentar. Las macros del SDK (`LoadView`/`WaitTOF`/`OwnBlitter`) llaman por el **global `GfxBase`**: es la fuente única (guardar la base de `OpenLibrary` en otra variable y dejar el global a 0 = `JSR (a6,-0xde)` con `a6=0`, crash real de la 066).
21. **Visión + diff objetivo por zonas, siempre juntos**: la visión detecta bien la falta de movimiento de entidades medianas/grandes, pero da **falsos negativos con objetos pequeños** (la joya de 20 px de la 066: la vio quieta y se movía). La verificación fiable es la pareja: pregunta dirigida al modelo + **diff de píxeles por zona** entre frames (`out/tmp/zone-diff-066.mjs` como referencia); un "no se mueve" de la visión se **confirma o refuta** con el diff (regla 3).
22. **Coherencia de setup antes de `mark_ready`**: validar en `init` que lo construido es coherente — regiones de memoria sin solapes (`RegionTable`), `BPLCON0` vs planos emitidos (`bplcon0_for` + `check_bplcon0`), orden de WAITs (`check_copper_wait_order`), combinación de cada `Bob` cubierta por el ejecutor (`check_bob_plan`), parcheo de sprite a media línea vs planos (`check_sprite_line_data`) y X del rearmado por línea (`check_sprite_line_hpos`) — y fallar con código (`mark_failed`, base `0x7C0xx`) en vez de ver basura. Detalle y catálogo: `docs/engine/architecture/COHERENCIA_DE_SETUP.md`; test: HOST-441. **La copperlist estática se emite UNA vez y se parchea** (la 066 pasó de ~10k a 340 ciclos/frame); re-emitirla cada frame es coste regalado.
23. **El frame lo marca el engine**: el bucle del engine llama a `mark_frame` (bajo `ENG_AMIGA`) con el `frame_index` del `GameContext`; la demo **no** necesita llamarlo en `update` (si lo hace, es idempotente). `mark_ready`/`mark_failed` sí son responsabilidad del `init` de la demo (con `detail` para telemetría).
24. **El estado del juego va en BSS/estático, no en la pila de `main`**: `run_demo` ya deja `AmigaBackend`/`DemoHost`/`App` (decenas de KB) en la pila; el estado del juego (modelos `Hud<>`, buffers, arrays de entidades) se declara **estático a nivel de fichero** (p. ej. `g_game`) y `main` solo construye el `GameDisplay` y llama a `run_demo`. Así la demo no depende de la pila que le dé el lanzador (el runner fija 128 KB; un lanzador AmigaDOS normal da mucho menos) y escala con mundos grandes. Un local en `main` puede funcionar en un lanzador y fallar en otro; además, el síntoma «datos que se corrompen» es fácil de atribuir a la pila cuando la causa suele estar en el render (orden de pintado, fondo sin restaurar): **verifica el alcance antes de culpar a la pila**.

## 4. Errores del engine que no son de tu turno

- Al **ejecutar cualquier demo** (propia o ajena), correr sus gates; si algo falla: **registrar DT** (síntoma, comando, evidencia), **avisar al usuario** y, si es del área que tocas, arreglarlo en una fase aparte.
- **Barrido periódico de flota** (p. ej. al cerrar un hito): `tools/run-host-tests.sh` completo (tests + gates) y `tools/test-regression.sh` global. Todo rojo nuevo o preexistente que aparezca se registra en DT.
- **Calidad por demo** (¿se ha empeorado? ¿merece la pena?): al tocar una demo, correr
  `node tools/debug/audit-demos-quality.mjs <id>` (fps vs baseline, suelo de 25, uso de
  hardware, analyze/visión con `--analyze --vision`) y `node tools/debug/check-fps.mjs
  --demo <id> --warn-only`. El listado vivo de demos < 25 fps (a mejorar/eliminar) y los
  criterios están en `docs/guides/roadmap/DEMOS_A_ELIMINAR_O_MEJORAR.md`.
- Los fallos de gates con baseline obsoleta (cast/doc-coverage/raw-pointer) se **regeneran solo justificando**, como indica cada gate.

## 5. Checklist rápido de una demo nueva

```
[ ] F0: intención + elementos esperados (colores únicos, marcador de movimiento) + presupuesto
[ ] F1: build/run/analyze verdes + telemetría detail + analyze-sequence --expect-animated
[ ] F2: por etapas (CPU primero) y cada etapa en presupuesto
[ ] F3: test-regression + measure-fps (50 fps / 1 field) + flicker + tests host
[ ] F3: presupuesto por elemento (worst-case de rutas no frecuentes) medido
[ ] F4: secuencia densa + prompts A/B/C + ojo del agente + check-elements/band-diff
[ ] F5: VALIDATION.md (prompts, respuestas crudas, medidas, conclusiones, excepciones) + README
[ ] F5: deuda abierta registrada en ROADMAP_DEUDA_TECNICA (propia o ajena)
```
