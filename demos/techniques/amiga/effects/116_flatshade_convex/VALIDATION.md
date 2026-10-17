# 116 - flatshade-convex: informe de validación

Informe de validación de la demo (procedimiento F0-F5). Pasada de modernización 2026-10-17. La demo queda **ABIERTA** por presupuesto (19,22 fps release < suelo 25 del efecto de relleno de polígonos): detalle y plan en la deuda **DT-026** (`docs/guides/roadmap/ROADMAP_DEUDA_TECNICA.md`).

## Medidas (`tools/debug/measure-fps.mjs`, 15-20 s)

| Build | fps emulado | campos/frame | ciclos/frame |
|---|---|---|---|
| HEAD (antes) | 19,20 | 2,60 | 369.564 |
| Modernizada (zona HUD por WAIT) | 19,70 | 2,54 | 360.056 |
| Modernizada (HUD de plano uniforme, fix gráfico) | 18,05 | 2,77 | 392.909 |
| `A500_debug` (HUD con fps del último segundo) | 18,05 | 2,77 | 392.909 |
| **`A500_release` (`-O2`)** | **19,22** | **2,60** | 369.168 |

Referencia original medida en las mismas condiciones (ADF, ciclo-exacto, perfil con capturas por campo): **2,0 campos/frame (25 fps)** — parejas de campos idénticos; CPU saturada (142102/142112 ciclos/campo) y ~62k accesos DMA/campo. La brecha release vs original (~0,6 campos/frame) es **CPU +45 % de accesos/frame** y **fetch +~0,2 campos/frame**; el desglose y el plan de cierre (async + asm) están en el README §«Contra la referencia original» y DT-026.

Cambios de esta pasada: `run_frames` (IRQ + `Wait()`, sin sondeo de VPOSR), `set_blit_service_enabled(false)` (la demo no usa la cola de fondo) y **franja de depuración de coste cero**: **5º plano de 1 bpp activo todo el frame** (etapa de `scene::compose`; sin zonas de Copper — ver el fix gráfico abajo), texto estático + campos dinámicos (contador de frames + fps del último segundo) ~1 vez/segundo; el render por frame no cambia. El fetch del 5º plano cuesta ~0,23 campos (-1,65 fps) y es el precio de la corrección (el plano alternativo «zona por WAIT >255» no es cerrable). El **fps del propio HUD** se validó contra `measure-fps` en régimen estable (`F217 18.1` con 18,05 medido; delta de `frame` del `run-report.json`: 219−164 = 55 en 3,08 s = 17,9).

## Fix gráfico del HUD (2026-10-17, urgente)

El HUD de la primera pasada activaba el 5º plano con un WAIT `>255` al final de la copperlist **estática** del `Scene`: con la semántica `>=` del comparador de WinUAE (`custom.cpp:4416` `coppercomp`) el disparo caía en la raster 255 y la lista **terminaba ahí (STOP)**: el 5º plano quedaba **encendido para siempre** y la pantalla mostraba franjas verticales de basura (memoria del plano fuera de su banda). Arreglo estructural: **BPLCON0 de 5 planos en la cabecera, activo todo el frame** (sin WAITs/zona), plano del HUD de tamaño completo (8 KB, el fetch recorre 256 filas) y **paleta espejo 16..31** (palette[16] = tinta, palette[16+k] = palette[k]) para que el +16 del 5º plano sea transparente en la escena. La franja queda en la banda superior (filas de ventana ≈16..23, libre del balón).

## Experimentos acotados y descartados (medidos)

- **Fill XOR acotado a la caja del objeto** (soporte `bbox` nuevo en `flat_shade_xor`): 19,2 → **17,3 fps**. El fill acotado son 4 jobs (uno por plano) cuyo `wait_blitter` interno serializa la CPU con el Blitter dentro del update; el barrido completo es **un solo job** lanzado sin esperar. El recorte (~56k menos de fill DMA) solo paga con el referee async (DT-026).
- **Clear acotado por bbox del buffer** (ring de cajas por buffer): misma serialización por planos (4 jobs) → descartado por el mismo motivo y no medido por separado (el build que lo contenía es el de 17,3 fps junto al fill).
- **2026-10-17, medidos EN AISLADO (release, `A500_flatshade_*_release`)**: **clear acotado a la union de bboxes** (`FLATSHADE_CLEAR_BBOX=1`): **19,24 fps / 2,599 campos vs 19,26 / 2,596** del barrido completo → sin cambio: en la pipeline de 3 buffers actual el clear ya está oculto (la cuenta de «68k expuestos» de septiembre no aplica al árbol de hoy; los cortes de fila/columna del clear no tocan el camino crítico). **fill acotado a bbox** (`FLATSHADE_FILL_BBOX=1`, pasa `g_bbox` a `flat_shade_xor`): **16,81 fps / 2,975 campos → peor**, confirma en aislado que la serialización de los 4 jobs se come el recorte. El umbral de 25 fps está en líneas (~114k) + fill completo (~131k), no en el clear.
- **Referee async completo** (`FLATSHADE_ASYNC=1`): reconstruido y sin cuelgue (3 causas raíz corregidas: horizontales en el plan, feeder despachado desde tarea via `wait_blitter` —fix de engine `g_blit_service_referee_only`— y giro de espera sin BLTPRI), pero **8,9 fps / 5,6 campos vs 18,05 / 2,77**: ~51 jobs cortos de línea pagando ~3,5-4k cada uno (arranque de Blitter + IRQ de nivel 3 + re-programación en la ISR) que la vía síncrona no paga. Ablación «solo líneas»: 4,15 campos. La vía queda para cadenas cortas de blits largos (066/212); default sigue la síncrona.
- **Original de referencia MEDIDO (ADF, perfil por campo)**: **2,0 campos/frame (25 fps)** — parejas de campos idénticos; CPU saturada (142.102/142.112 ciclos/campo); ~62k accesos DMA/campo. Cierra la duda de septiembre («¿el original también ronda 20 fps?»: no, da 25). El port release (2,60 campos) está a ~0,6 campos: CPU +45 % de accesos/frame y fetch +0,2 (HUD).

## Campaña de paridad por el API de alto nivel (2026-10-17, release)

| Paso | fps | campos/frame | Δ |
|---|---|---|---|
| baseline (tras fix contadores/HUD uniforme) | 19,21 | 2,60 | — |
| + HUD banda superior legal (WAIT 44/58) | 20,90 | 2,392 | +1,69 |
| + `wait_blitter` restaura BLTPRI (no lo apaga) | 21,23 | 2,355 | +0,33 |
| orden «clear antes del transform» (probado) | 20,43 | 2,447 | −0,80 → descartado |
| orden «clear al principio del siguiente update» (probado) | 21,01 | 2,380 | −0,22 → descartado |
| + `-O3` en la TU de la demo (`build.args`) | 21,54 | 2,321 | +0,31 |
| + long-writes en `blitter_line_draw` (con0/1, bmod/amod) | **21,62** | **2,313** | +0,08 |
| + `BLTDPT` fijo en el preludio (`blitter_lines_begin(row_bytes, d_base)`) | 21,73 | 2,301 | +0,11 → **REVERTIDO**: rompía la paridad del contorno (rayas en fases concretas; el Blitter muta DPT durante el blit). Ver bitácora — la reescritura por línea NO era redundante. |
| + fill acotado por bbox en UN blit contiguo (`blitter_area_fill(..., bbox)`, primitiva nueva) | 21,72 | 2,302 | ≈0 (el ahorro cae en la cola absorbida del fill) → se conserva por menos bus |
| re-test del orden «clear primero» de Grok CON BLTPRI restaurado | 21,02 | 2,379 | −0,71 → refutado ×2 (ver bitácora) |
| interleaving cooperativo del transform en las esperas del Blitter (ronda 2 de Grok) | 14,72 / 14,80 | 3,40 / 3,38 | muy peor en ambas variantes (global y con ventana) → refutado y aparcado tras `FLATSHADE_PUMP=0`; el bloque síncrono ya corre bajo el fill (ver bitácora) |

Con las palancas fijadas (hoist de DPT revertido), remedidas: **release 21,62 fps / 2,313 campos** y **debug 20,51 / 2,438**. La regresión del hoist se cazó con **secuencia de 8 frames** (rayas horizontales en fases de rotación que el `freeze-diff` de un único ángulo y la visión sobre 2-3 fases no cubrían); tras la reversión, secuencia + visión (4 fases) y capturas manuales limpias. El fill acotado por bbox (primitiva nueva `blitter_area_fill(..., bbox)`) quedó validado **pixel-exacto** contra el barrido completo con A/B congelado: `freeze-diff` = 136 px distintos **todos en la fila del texto del HUD** y **0 diferencias en el balón**. `ENGINE_OPT=-O3` probado: 21,61 vs 21,62 → sin cambio, no se aplica. Protección registrada (bitácora): en modo línea WinUAE lee `bltapt`/`bltcon*`/mods EN VIVO durante el blit (`blitter.cpp:731-745`, `:1097-1110`; solo `BLTSIZE` reinicia, `custom.cpp:3981`) → la espera por línea es del hardware, no se puede solapar la programación (a diferencia de los jobs de estado fijado del feeder async). Triaje completo de la consulta a Grok en la bitácora (`OPTIMIZACION_GPP_68000.md`): 1 refutada ×2, 1 aplicada (DPT), 2 descartadas con dato. Visual tras el hoist: secuencia + visión sin anomalías («caras planas con contornos, sin rayas/huecos/vértices sueltos», HUD `F52 20.3` en debug).

Visual tras los cambios: secuencia + visión (`qwen3-vl`) sin anomalías («caras planas azules con contornos, sin rayas/huecos/líneas cruzadas»); captura completa y lupa del HUD limpias. Suelo del pipeline actual (ablaciones): transform+overhead 142k (1 campo), fill ~66k, edges ~112k, clear ~100k expuestos; límite teórico ~262k = 1,85 campos ≈ 27 fps con solape perfecto. Holgura restante (~44k): esperas por línea del contrato de registros del Blitter + cuantización a VBlank.

**Cirugía de codegen (2026-10-17, cierre): release 21,62 → 21,67..21,72 fps (2,312 → 2,302..2,307)** con `eng::math::arith<s16>::mul/mulu` y `row_offset` en su forma de operandos `s16` directos (el cast a `s32` del constraint empatado forzaba `ext.l` + copia por operando: `ext.l` en el update 12→1, `mul32` total 14 sin libcalls nuevos; la 101 playfield se mantiene en 49,77). Los knobs de IRA/scheduling (×4) midieron wash. Comparado con el **binario original como oráculo** (`tools/analyze/hunk-image.mjs` + objdump por funciones): su `TransformVertices` es ~30 instr/vértice **sin pila** (matriz con puntero y `(a0)+`) y el nuestro aún lee la matriz por dirección absoluta con el cache del proyector en pila — candidatos siguientes de cirugía (matriz lineal `s16[12]` caminada por puntero; cache fuera de pila). Detalle: bitácora `OPTIMIZACION_GPP_68000.md` (entradas 2026-10-17) y DT-026.

## Ruta asm (estado 2026-10-17)

Octante de `fs_draw_edges` corregido (usaba `x1 < x0` en vez de `x0 >= x1` para AUL/SUL; contraste `line_prepare_impl`). **La ruta sigue NO VALIDA**: `-DK_FLATSHADE_ASM=1` dibuja rayas y no avanza frames (wireframe de referencia: era ~1-2 px de desfase en septiembre). El default C++ no se toca: 18,12 fps debug / 19,26 release con contorno correcto (`final116.png`). Diagnóstico siguiente: `freeze-diff` de `fs_transform_vertices` vs `lib3d` sobre el wireframe (`FLATSHADE_SKIP_FILL=1`).
- Se conserva el **barrido completo** de fill y clear (single-job, `wait=false`), con comentarios de coste en `src/main.cpp`.

## Validación visual (secuencia + visión)

- Secuencia: `run-demo.sh --sequence-frames 2 --sequence-interval-ms 3000` tras 9 s de asentamiento; evaluación canónica en `src/VISION.md` (`qwen3-vl:8b-instruct-q8_0`).
- Respuesta (resumen fiel de la cruda): poliedro con **caras planas azules** de tonos variables (sombreado plano) girando sobre fondo oscuro, texto blanco `FLATSHADE v2.0.<build>  F<frames>  <fps>` en la banda **superior**; **sin artefactos, recortes ni superposiciones**; secuencia coherente de rotación.
- Comprobación propia: capturas separadas con el fps del HUD (`F217 18.1`, `tools/debug/hud-zoom.mjs` para leer los dígitos) que casa con `measure-fps` (18,05) y con el delta de `frame` del `run-report.json`; el render del balón no se ha tocado en esta pasada (misma llamada `flat_shade_xor` a barrido completo, mismos transforms).

## Fidelidad

- La pasada no modifica el render del sólido (la técnica de contorno EOR + area fill XOR, los transforms y la composición quedan igual); el único cambio de imagen es la **franja de depuración** (5º plano de 1 bpp, banda superior, filas ≈16..23) con sus campos dinámicos.
- El gate de fase congelada (`FLATSHADE_FREEZE_ANGLE` + `tools/analyze/freeze-diff.mjs`) sigue siendo el contrato determinista: aplica si se toca el dibujo (p. ej. al portar el fill al plan async).

## Límites del alcance (SIN VERIFICAR / abierto)

- **Presupuesto**: 19,22 fps release < 25 → demo **ABIERTA** (DT-026). La deuda no es de corrección visual sino de camino de ejecución (referee async del `FramePlan`; hay un prototipo async iniciado en el turno del 2026-10-17 que quedó **colgado** en la cadena de jobs y se retiró — la base del engine (`BlitJobKind::AreaFill`) quedó añadida para retomarlo).
- El fps del HUD se contrastó en régimen estable (una ventana tardía); no se verificó en las primeras ventanas tras READY.
- No se declaró `flicker-baseline.json` en esta pasada.
- La ruta asm (`K_FLATSHADE_ASM=1`) sigue sin ser válida (desfase de contorno, README): no se ha tocado.
