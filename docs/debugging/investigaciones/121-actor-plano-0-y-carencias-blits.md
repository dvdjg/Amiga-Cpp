# 121 — Actor del carril: pérdida intermitente del **plano 0** al mezclar copias y cookie-cut en un `FramePlan` (y carencias del motor de blits)

Bloqueo **cerrado con mitigación** en la demo 121 (`proc structures`, cocido procedural + motor de tiles X-Limited) y **abierto en el motor**: la causa raíz del plano perdido no está reproducida en host todavía; el patrón que la dispara está identificado y la demo lo evita.

## Síntoma

El actor de la demo (bicho de 16×16 dibujado por Blitter con cookie-cut y máscara combinada `actor & ~copas`) aparecía **invisible o incompleto** en las capturas de forma intermitente y dependiente de la posición; la guarda de render por frame y el auto-chequeo de init registraron el valor observado:

| Codificación (`detail`) | Punto | Valor | Lectura |
|---|---|---|---|
| `0x1F2102` = `0x12102 \| sub<<8 \| body<<16` | auto-chequeo, restaurador + actor en el **mismo** plan, `sub=0` (px 112) | `body=0x1E=30` | falta el **plano 0** del cuerpo |
| `0x1E01252F` = `0x1212F \| x<<8 \| body<<24` | guarda por frame, `x≈260` (0x104) | `body=30` durante ≥3 frames seguidos | falta el plano 0 |

`body=30` (binario `11110`) = los planos 1..4 escritos y el **plano 0 sin escribir**. El fondo restaurado del carril en ese píxel tiene el bit de plano 0 a 0, de modo que **el fallo es invisible cuando el BIT DE PLANO 0 del fondo vale 1** (el plano obsoleto coincide con el correcto): por eso se manifiesta solo en ciertas posiciones y de forma intermitente entre arranques.

## Qué se descartó (con evidencia)

- **No es el arte ni la paleta**: el render congelado (mismo camino de blits, sin restaurador por frame) da **552 px `#EEEEEE` exactos** en bbox `x 112..127`, `y 144..159`, con el ojo `#443300` (índice 2) en su sitio, y la visión local (Ollama, `qwen3-vl`) confirma «sprite blanco con dos ojos oscuros sobre el carril central; sin defectos: planos alineados, sin rastros ni partes duplicadas o faltantes» (informe crudo en `demos/techniques/amiga/effects/121_proc_corner_layers/121_proc_corner_layers_report.md`).
- **No es el emisor ni el encoder**: la equivalencia racha (`BlobBatch`) ↔ encoder canónico está cubierta en `HOST-451`/`HOST-241`, y los modulos/registros del lote están correctos (`blob_batch.hpp`: `kBltcmod=$060`, `kBltamod=$064`, con los pares CMOD:BMOD y AMOD:DMOD contiguos).
- **No es el actor solo**: las sondas de actor **sin** restaurador previo (pasos 3 y 4 del auto-chequeo: 16 subposiciones y 5 columnas) **nunca** fallaron en ningún arranque.
- **No es la lectura**: el fallo persistió con re-lectura inmediata (3 pasadas) — es el bitmap, no el CPU.
- **No es la caché de comunes** de `submit_blit_job` (desactivada en ese punto, `amiga_blitter.cpp:450`).

## Patrón que lo dispara

En el **mismo** `FramePlan`: 5 jobs `CopyRect` (restaurador del carril) seguidos de una racha de 5 × `MaskedBobCookieCut` (los planos del actor). El lote de racha (`BlobBatch`, `amiga_blitter.cpp:21-53`) arranca tras el camino genérico (`amiga_blitter.cpp:326+`); el plano perdido es **siempre el primero de la racha**. Separando restaurador y actor en **dos** `execute_frame_plan` (cada uno espera su último blit), el fallo desaparece: **6/6 arranques limpios** (3 con restaurador de banda completa + 3 con restaurador de ventana).

Pendiente: test host que reproduzca la **secuencia mixta** de registros (copias → racha cookie-cut) sobre un estado del Blitter simulado; si el host demuestra equivalencia, la hipótesis pasa al emulador (interacción del rearme del lote con el blit de copia anterior / `DMACON`/`BBUSY` con `cpu_cycle_exact`+warp). Hasta tener la causa raíz **no se ha tocado el motor**: ver carencias y sugerencias.

## Bisección en hardware (2026-10-16): plan multi-job vs plan por objeto

El **modo golden** de la demo (`K121_GOLDEN_ACTORS`, 16 subposiciones a la vez, paleta identidad)
con el oráculo independiente `tools/analyze/verify-121-actor-golden.mjs` permite comparar formas de
plan con el mismo render:

| Variante | Resultado (oráculo) |
|---|---|
| Un plan con las 16 subposiciones (80 jobs = una racha cookie-cut de 80 objetos) | Caótico: 819 px del índice del cuerpo (vs 2060 esperados), subposiciones desplazadas (fracaso total en 6) |
| Un `execute_frame_plan` por actor (16 planes de 5 jobs) | 10/16 subposiciones **exactas** (0 píxeles de actor fallidos); 6 perdidas/parciales (cols 0/3/4/6/7/9) + 127 px de daño colateral |
| Un plan por actor, con el contrato de espera por job (`blitter-busy-writes.md`, en curso) | Igual que el anterior: el colateral del plan multi-job desaparece en el caso multi-job (127→3 px), pero los 6 actores perdidos persisten |

Lectura: hay **dos síntomas distintos** en la misma familia. (1) El encadenado sin esperar a `BBUSY`
pierde/desplaza objetos de los grupos largos (corregido por el contrato de espera por job;
`docs/reference/emulators/winuae/blitter-busy-writes.md`). (2) Incluso con planes cortos y tras ese
contrato, algunos actores NO se dibujan (o se dibujan ~2 celdas a la izquierda: los 127 px de colateral
en x=105) — pendiente de aislar; el golden deja el banco listo para validar cada iteración del motor.

## Mitigación en la demo (121)

1. **Dos planes por frame**: `update` ejecuta primero el restaurador y después el actor (coste: +1 `execute`/frame); el auto-chequeo (paso 5) hace lo mismo.
2. **Restaurador de ventana**: en vez de repintar las 18 celdas del carril, se copia una **ventana de 2 palabras** en la posición del frame anterior (`m_restore_x`, span apuntado a `m_lane_bg + x/16` palabras con el mismo `src_row_bytes`/stride) — ~1/9 del trabajo de antes.
3. **Reintento único** en el auto-chequeo y **3 strikes consecutivos** en la guarda de render por frame (un fallo real falla todos los frames; los picos de un solo plano se descartan).
4. Limpieza post-auto-chequeo: una restauración completa de la banda antes de `READY` (el chequeo deja al actor dibujado en varias posiciones).

Con la mitigación: **3/3 medidas limpias a 47.67 fps (1.049 campos/frame)** — el split cuesta ~6.7k ciclos/frame frente al plan único (`142k`), dentro del presupuesto de frame (199k), con frames ocasionales a 2 campos.

## Carencias del motor (por qué no evitó el problema)

1. **Sin contrato de sincronía para lectura/observación**: `execute_frame_plan` espera al último blit, pero no existe un punto de verificación documentado (ni API) para leer el bitmap con garantías; las demos acaban auto-chequeándose a mano.
2. **Sin detección de blits que no escriben**: el motor acepta y ejecuta el plan aunque un plano no se escriba; no hay modo verificación (re-ejecución + lectura diferencial) que convierta «un plano perdido» en un error localizado.
3. **Rachas sin test de secuencia mixta**: existe equivalencia racha↔encoder (`HOST-451`) pero no cobertura del **encadenado** copia→racha en un plan (el patrón que falla).
4. **Semántica del anillo no validada**: `add_world_bitmap*` toma coordenadas de mundo/anillo con bias de guarda (`visible_tile_bias_x/y`) que no está en la firma ni la valida `eng::setup`; equivocarse (fue el caso: faltó `kRingGuardY`/bias X) produce desalineación silenciosa.
5. **Sin restauración por ventana en el emisor**: el patrón salvar/restaurar ventana (borrado barato de un objeto que se mueve) no existe como API; se resolvió con aritmética de span sobre la banda completa.
6. **Oclusión sin soporte declarativo**: la máscara combinada (`actor & ~copas`) se construye a mano en la demo; no hay emisor «dibuja bajo capa» que promedie esa lógica.
7. **Telemetría insuficiente**: `g_eng_run_status.detail` no tiene campos reservados para estado de render por frame (se usan codificaciones ad-hoc por demo).

## Carencias de las herramientas (costaron horas de diagnóstico)

1. **`--sequence-step-frames` captura a mitad de frame**: la pausa en el `ready-probe` + screenshot entrega estados donde los blits del frame **no están reflejados** (actor ausente o con «motas»), y la secuencia completa parecía demostrar un defecto de render inexistente. Lo correcto era la captura simple tras `READY` (mostró el actor completo) o pausar tras `wait_blitter`.
2. **`--config <id>` se ignora silenciosamente**: fuerza `A500_debug` aunque se pida otra config (y a veces el «mejor» build por timestamps la sombrea) — verificado copiando binarios a mano.
3. **Crash intermitente del runner** tras `READY` (`[WinUAE] Process exited with code null`, report sin `finalSideChannel`) — deja capturas viejas con apariencia de nuevas. Además, una **instancia huérfana** de un run anterior puede quedar escuchando en los puertos y **servir estados viejos** al run siguiente (fallos idénticos repetidos con el mismo `detail` en arranques consecutivos; con higiene de procesos antes de cada run: 9/10 limpios).
4. **MCP WinUAE**: `connect`/`connect_existing` con `config/mcp-amiga-c-debug.uae` expiran aunque el puerto GDB está `LISTENING`; en el lanzamiento manual apareció una ventana «WinUAE message» (posible diálogo modal bloqueando el handshake).

## Recomendaciones priorizadas (motor robusto)

1. **Test host de la secuencia mixta** (copias + racha cookie-cut en un plan, registro a registro y resultado por plano): el candidato nº1 para localizar el plano perdido; extiende `HOST-451`.
2. **Contrato + API de verificación**: `wait_blitter()` público de facto (documentado) y helper de auto-chequeo de render (leer píxel con re-lectura) reutilizable por todas las demos; los codificados ad-hoc de esta demo son el germen.
3. **Validador de geometría de anillo** en `eng::setup` para `add_world_bitmap*` (bias_x/bias_y, rect dentro del anillo, costura) con fallo temprano.
4. **Emisor de restauración por ventana** y **emisor con oclusión** en el playfield (los dos patrones de la demo que hoy van a mano).
5. **Arreglar las herramientas**: `--config` real, captura esperando `wait_blitter`/frame completo, y handshake MCP con la config del repo (revisar el diálogo «WinUAE message»).

## Estado final (2026-10-16)

- **Causa raíz (motor)**: escribir registros del Blitter (BLTSIZE) con un blit activo lo reinicia/pierde — documentado con `fichero:línea` en `docs/reference/emulators/winuae/blitter-busy-writes.md` y corregido con el contrato de espera por job (commit `052f17e4`).
- **Demo 121**: vuelta al **plan único** verificada 7/7 arranques limpios (47,67 fps); la sonda, la guarda, el validador de anillo y el emisor de ventana quedan como herramientas permanentes de la demo.
- **Banco golden**: el modo `K121_GOLDEN_ACTORS` + `tools/analyze/verify-121-actor-golden.mjs` compara 16 subposiciones contra un oráculo JS independiente. Con máscara propia por slot (bug del propio golden corregido: los 16 dibujos del plan compartían `m_mask`): **10/16 exactas**; persisten 6 (cols 0/3/4/6/7/9, una parcial) y ~127 px de daño colateral en x≈105 — anomalía residual abierta con banco listo para reproducir y validar el fix.
- **Herramientas**: `--config` funciona (escribe en `out/run/<demo>/<CONFIG_ID>/`); step-capture sirve estado previo al dibujo (no usar para validar render); capturas simples + telemetría + golden son las vías fiables.

## Enlaces

- Demo: `demos/techniques/amiga/effects/121_proc_corner_layers/` (`VALIDATION.md` con las medidas y el informe de visión).
- Motor: `engine/src/platform/amiga/amiga_blitter.cpp` (ejecución del plan y rachas), `engine/include/eng/platform/amiga/blob_batch.hpp`, `engine/include/eng/graphics/blitter_state.hpp` (`blob_run_from`/`blob_run_continues`).
- Tests relacionados: `HOST-451` (`tests/host/platform/amiga/451_blit_runs`), `HOST-241` (`tests/host/platform/amiga/241_blob_batch`).
- Deuda registrada: `docs/guides/roadmap/ROADMAP_DEUDA_TECNICA.md` DT-022 (motor) y DT-023 (herramientas).

## Fix del motor (2026-10-16): contrato de escritura con el Blitter ocupado

El patrón encaja con la fuente primaria: el AHRM 3.ª («Multitasking and the Blitter») prohíbe
escribir registros del Blitter con un blit en curso, y WinUAE **reinicia** el blitter si se escribe
`BLTSIZE` con uno activo (`../WinUAE-DBG/blitter.cpp:2036` aviso, `:2058` re-init, `:2101`/`:2119`
contadores a cero). El plan encadenaba jobs sin esperar; eso solo «funcionaba» cuando cada blit
acababa antes de la siguiente programación (timing del bus), y en un burst tras un blit largo se
pisa el estado del que corre — el plano perdido es el primero del burst. Ficha del hallazgo:
`docs/reference/emulators/winuae/blitter-busy-writes.md`.

Fix aplicado: `submit_blit_job` espera `BBUSY==0` antes de programar cada job (las rachas
`BlobBatch` ya esperaban por objeto). Coste medido en la 213: nulo dentro del ruido (sync 32,30 vs
32,20–32,32 fps; async 27,41 vs 27,58). Alcance declarado: el A/B local con plan único quedó
**contaminado por DT-023** (instancias huérfanas sirviendo estado viejo y `--sequence-step-frames`
capturando a mitad de frame); la reproducción limpia corresponde a la verificación golden de la
demo (`K121_GOLDEN_ACTORS` + `tools/analyze/verify-121-actor-golden.mjs`). Con ella, si el plan
único sale limpio, la mitigación de dos planes puede retirarse.
