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
4. **Nada de pintar con borrado/repintado sobre un lienzo visible single-buffer**: o se pinta dentro del VBlank con margen, o se dobla el buffer. Un hueco de borrado mayor que el barrido de la zona = flicker visible.
5. **Actualizar por señal de VBlank** (`eng::os` + `run_frames`), no por sondeo, salvo fallback justificado.
6. **Cada rojo de un gate se registra** en `ROADMAP_DEUDA_TECNICA.md` con síntoma + evidencia (aunque no sea tu tarea y aunque sea previo). No se "sigue adelante" con un rojo sin registrar.
7. **No maquillar la medida**: no relajar umbrales, no alterar telemetría ni cadencia para simular cumplimiento.
8. **Excepciones de presupuesto solo documentadas** (nunca por debajo de 25 fps) con motivo y medida en el `VALIDATION.md`.
9. **Nada de "ya se optimizará después"**: cada etapa deja el frame en presupuesto, con el peor caso medido (incluidas las rutas que no se ejecutan siempre).
10. **Cada demo tiene su informe de validación** (`VALIDATION.md`): prompts de visión + respuestas + medidas + conclusiones; se actualiza en cada pasada que cambie su render o su coste.
11. **Usar el modelo de visión local (Ollama) sin límite** para todo lo que aporte (inventario, dinámica, dirigido, dudas sobre frames): es local y gratis; ante la duda, otra pregunta y otra pasada.

## 4. Errores del engine que no son de tu turno

- Al **ejecutar cualquier demo** (propia o ajena), correr sus gates; si algo falla: **registrar DT** (síntoma, comando, evidencia), **avisar al usuario** y, si es del área que tocas, arreglarlo en una fase aparte.
- **Barrido periódico de flota** (p. ej. al cerrar un hito): `tools/run-host-tests.sh` completo (tests + gates) y `tools/test-regression.sh` global. Todo rojo nuevo o preexistente que aparezca se registra en DT.
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
