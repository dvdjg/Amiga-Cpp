# Roadmap: deuda técnica

Registro de deuda técnica **transversal** detectada durante el desarrollo (no ligada a una única feature/roadmap de dominio). Fuentes y reglas:

- Cada entrada lleva: **síntoma**, **evidencia objetiva** (medida reproducible), **impacto** en la regla correspondiente, **plan por fases** y **estado**.
- Las demos que no cumplen `docs/guides/roadmap/ROADMAP_UNIFICADO.md` §«Objetivos de rendimiento y estabilidad visual» quedan **NO VERIFICADAS** hasta cerrar su entrada.
- Una demo con capturas incoherentes no se marca como validada: la validación visual exige contraste con la intención y verificación por el agente (ver `docs/guides/methodology/DEMO_VISUAL_DEBUG.md`).

## DT-001 · Demo 110 (`ylimited_shooter`): scroll, cadencia y FG rotos

**Estado: ABIERTA — F1 hecho (atribución por ablación); F2 reclasificado como bug de engine (DT-006).** Reportada por el usuario tras ver la demo en WinUAE; confirmada por medidas.

Síntomas (reporte en vivo + medidas propias):

1. **Cadencia muy por debajo de 50 fps**: `measure-fps` da **11,53 fps / 4,34 fields/frame** (615.160 ciclos/frame) en `A500_debug` y **14,63 fps / 3,42 fields** (484.990 ciclos/frame) en `A500_release`. El presupuesto PAL es 1 field (~141,9k ciclos). Evidencia: `out/playfield/...` JSONs de `tools/debug/measure-fps.mjs` (detalle `0x110000ee`/`0x11000046`).
2. **Scroll roto**: la banda inferior de 64 líneas Amiga (y=224..287) está **completamente estática** mientras el resto scrollea (barrido por bandas de 16 px sobre una secuencia de 40 frames: `diff_medio=0.00` solo en y=0..15 y y=448..575 del PNG 2x). El usuario ve además **tres bandas de tiles abajo que no se mueven y parecen bugeadas** (pendiente de mapear a línea Amiga exacta con captura de la zona).
3. **Sin scroll fino horizontal**: la política configurada es `AxisPolicy::Finite` («puntero directo; no repinta»), que no ofrece fine scroll; el vaivén X de la cámara se ve a saltos.
4. **Flicker de FG**: la torreta verde (y≈36) desaparecía/queda a medias en ~50% de las capturas y la nave era invisible (`ship_y = 208` fuera de un lienzo de 208 filas). Mitigado a nivel de demo (nave visible, torreta repinta sin borrar el cuerpo, franja de 1 px en la nave), pero el lienzo FG es **single-buffer** y se actualiza durante el barrido: el fix real es doble buffer del FG con flip en la copperlist.
5. **Sprites parpadeando** (reporte del usuario): pendiente de caracterizar por separado (¿FG single-buffer, caída de frames, o sprites HW del BG?).

Causa raíz conocida hoy: la demo/engine hace **mucho más trabajo por frame que el presupuesto** (3-4 fields) y pinta capas vivas sin doble buffer; con frames de 3-4 fields, cualquier escritura durante el barrido produce tearing/parpadeo visible.

Plan **incremental** (una fase por pasada, cada una con medida y validación):

- **F1 · Perfil y presupuesto. HECHO (ablación + secciones):** sin el bloque FG la demo pasa a **39,9 fps / 1,25 fields / 178k ciclos**; con FG, 485k → **el FG cuesta ~307k ciclos/frame (63%)** y el resto ~178k (25% sobre presupuesto). `profile.mjs` no resuelve `g_eng_prof` en estos builds (DT-008). Camino: rediseñar el FG (pre-render + Blitter/BOB o sprites) y recortar el resto.
- **F1-bis · Atribución por secciones (2026-10, perfil ya operativo):** total **426.733 ciclos/frame** (16,62 fps). Secciones: **FG 230k** (nave+balas ~120k, torreta ~108k), scroll (`update_scroll`) **44k**, compose **18k**, plan BG **10,7k**, puntería (trig) 1,3k; resto espera. **Bisect del FG:** con sink de relleno VACÍO el bloque sigue costando ~97k → **cada `fill_rect` cuesta ~6,5k ciclos por LLAMADA** (experimento: +10 fills de 2×2 = +65k) y cada **job de Blitter ~3-5k** (versión diferida de 60 jobs = 850k). El relleno CPU por palabras tampoco salva el bloque (las escrituras a Chip con el display DPF activo suman ~130k). Lecciones generales en `docs/guides/optimization/ATRIBUCION_DE_CICLOS.md`.
- **F4 · Rediseño del FG. HECHO (2026-10) — 12,49 → 27,41 fps:** hojas pre-renderizadas de nave y torreta (16 variantes de offset de palabra; la copia empieza una palabra a la izquierda y mide 4 → cubre posición nueva ∪ anterior, **sin job de borrado**) en un plan FG ejecutado al principio del frame, y balas/cañón por relleno CPU directo con **máscara de planos** (`fg_fill`/`fg_clear`; sin mults en el bucle). Medido: el coste real por operación es ~11k por job encolado y ~2-3k por relleno CPU (ver `ATRIBUCION_DE_CICLOS.md`). **Queda para 50 fps**: el scroll de motor (~45k, `update_scroll` XLimited — familia DT-006), compose (~16k) y el lanzamiento de jobs; camino definitivo = **FG por sprites HW** (F5: cero escrituras de lienzo). Visión OK (3 frames, sin artefactos).
- **F2 · Scroll. RECLASIFICADO:** las «tres bandas» inferiores son un **bug de engine de la familia XLimited** (la 202 las tiene igual) → **DT-006**. No es de la demo.
- **F3 · Fine scroll X.** Definir el contrato (`Finite` con `BPLCON1` fino) o documentar la limitación; la demo debe moverse suave.
- **F4 · FG sin tearing y en presupuesto.** El lienzo FG single-buffer dibujado por CPU es a la vez el hotspot (63%) y la fuente del flicker; candidato: pre-render + blit o canal de sprites/BOB.
- **F5 · Validación.** `measure-fps` a 50 fps, gate de flicker con baseline real, secuencia **determinista** (`--sequence-step-frames`) + prompts de visión (`DEMO_VISUAL_DEBUG.md` §6.4) + `check-elements --expect`.

Restricción de método: **no tocar a la vez scroll y rendimiento**; cada fase cierra con medida (fps/ciclos), captura y visión.

## DT-002 · Presencia de elementos en la validación — RESUELTO

`tools/analyze/check-elements.mjs` admite `--expect <color>=<min>`: **falla** si en algún frame el número de píxeles de un color esperado baja del mínimo (gate de presencia de elementos). Uso obligatorio en F3/F4 del procedimiento (p. ej. `... 00ff00,ff4400 --expect ff4400=250 --expect 00ff00=900`). Complementa a `flicker-check`, que mide parpadeo de bloques y no pérdida de elementos.

## DT-003 · Protocolo de prompts de visión (referencia) — RESUELTO (ampliado)

Escrito en `docs/guides/methodology/DEMO_VISUAL_DEBUG.md` §6.4 (tres pases: A inventario **sin contexto**, B dinámica/seguimiento, C dirigida con la intención; prompts exactos, reglas —aviso de contenido bloqueante, sin coordenadas de píxel, guardar respuesta cruda— y límites medidos del modelo). El procedimiento completo que lo usa en cada demo/juego es `docs/guides/methodology/PROCEDIMIENTO_DEMOS_Y_JUEGOS.md` (fases F0–F5 y registro de errores ajenos al turno).

**Ampliación (informe 110):** el modelo **no atiende de forma fiable varias imágenes por mensaje** — con 4 imágenes declaró «ambos frames» y con 2 declaró «un único frame» — y da **falsos negativos en movimiento lento/patrones periódicos** («nave estática», «fondo fijo» con bbox y traslación demostrando lo contrario). Estrategia operativa en §6.4: una imagen por llamada o **hoja de contacto etiquetada** (una imagen), y la **evidencia objetiva decide** (`tools/analyze/check-elements.mjs`, `band-diff`, bbox/traslación). Informe de la demo: `demos/techniques/amiga/playfield/110_ylimited_shooter/VALIDATION.md`.

## DT-004 · `measure-fps` no resuelve símbolos en algunas demos

`measure-fps 202_xlimited_dpf A500_release` falla con `runtime=0x0`/«no se pudo resolver g_eng_run_status» (map/baseText), dejando sin control comparativo a 110. **Plan:** revisar la resolución por `.map`/`qOffsets` para configs release de esa familia o dejar el control con `A500_debug`.

## DT-005 · Captura de secuencia no determinista — RESUELTO

El runner ya soporta **`--sequence-step-frames N`** (`tools/run/run-demo.ts`): captura N frames **1 frame de juego aparte** con el breakpoint `eng_debug_ready_probe` (`frame_NNN_fNNNN.png` consecutivos). Es el modo obligatorio para validar movimiento/cadencia; el modo por intervalo queda para vistazos. Las medidas de `band-diff`/`check-elements` deben hacerse sobre la captura por paso.

## DT-006 · Bandas de tiles inferiores estáticas (familia XLimited) — diagnosticado, sin arreglar

**Síntoma (usuario):** tres bandas de tiles abajo que no se mueven y parecen bugeadas; «planos moviéndose cada uno a su lado». **Evidencia (2026-10):**

- **Aislamiento reproducible**: build de la 110 **solo-scroll** (FG comentado) con el mapa codificando la fila como glifo (`g_map = y & 15`) y captura determinista `--sequence-step-frames`. Desplazamiento vertical compuesto uniforme (−2 px/juego, `mad=0.00`), **pero** la ventana real muestra los **dígitos duplicados por filas** (1,1,2,2,3,3…) y una **banda basura** (roja/rayas) en la zona inferior: el área visible no se pinta correctamente.
- **Estado real de hardware** (dump de registros con MCP): `DIWSTRT=$2981`, `DIWSTOP=$F9C1`; `BPL1MOD=$0078` (120), `BPL2MOD=$004E` (78); separación de punteros **PF1 = 54 B/plano** ($020746/$02077C/$0207B2) y **PF2 = 40 B/plano** ($02B300/$02B328/$02B350). Las dos capas del DPF tienen **geometrías de fila distintas** (54 vs 40 B por plano) con un **DDF compartido**, y el `BPL2MOD` se calcula «para igualar fetch» (`xlimited_composer.hpp:481-484`) — principal sospechoso de la duplicación/desincronía por plano y de la banda inferior.
- **Herramienta de captura de ventana**: `mcp-winuae-emu/src/winuae-window-capture.ts` capturaba mal (en modo cliente ni intentaba `PrintWindow`); arreglado a `PrintWindow(PW_CLIENTONLY|PW_RENDERFULLCONTENT)` con DPI y verificado (`method=printwindow`). Requiere **reiniciar el servidor MCP** para que el proceso lo use.
- **Referencia**: algoritmo de corkscrew y layout en `C:\Users\dvdjg\Documents\programa\AI\Amiga\ScrollingTricks\ScrollingTricks\Docs` y ficha de registros/desviaciones en [`docs/reference/amiga/techniques/ylimited-corkscrew.md`](../../reference/amiga/techniques/ylimited-corkscrew.md) (AHRM cap. 3 `:1723-1729` para modulos; la referencia exige bitmap `(SCREENWIDTH+16) × (256+32)`).

**Estado:** causa localizada a geometría/mods DPF; siguiente paso leer ScrollingTricks + AHRM/emulador, corregir el compositor/base XLimited con el banco aislado y test de dígitos.

## DT-009 · Auditoría de flota de demos: inventario fps HECHO — hoy: remediar

**Herramienta hecha**: `tools/debug/audit-demos-fps.mjs` (sin argumentos = lista clave; `--all` = todas las construidas) → tabla en `out/fleet-fps/fleet-fps.{json,md}`. Primer barrido (2026-10, A500_debug):

| demo | fps | campos |
|---|---|---|
| 110_ylimited_shooter | **12.5** | 4.00 |
| 085_copper_plan_scene | **16.6** | 3.00 |
| 116_flatshade_convex | **19.4** | 2.58 |
| engine_amiga_065_mechanisms_bridge | **22.4** | 2.24 |
| 202_xlimited_dpf | 28.5 | 1.76 |
| 213_spr_layer | 36.5 | 1.37 |
| 212_free_scroll_layer | 38.1 | 1.31 |
| engine_amiga_066_bob_showcase | 39.2 | 1.27 |
| 101_ehb_tile_scroll_driver | **49.9** | 1.00 |
| 126_fast_bobs | **49.9** | 1.00 |
| 218_free_form_sprite_layer | **49.9** | 1.00 |
| 215_jim_power | **0** | — (explicado: la demo no llamaba `mark_frame`; el medidor cuenta por el contador de frames — **arreglado**, 49,97 fps) |

**Actualización (2026-10):** 085 y 215 resueltas (DT-007; 215 = `mark_frame` ausente); barrido
`mark_frame` aplicado a 34 demos que no marcaban frame (la telemetría es norma; sin ella
`measure-fps` da 0). Queda **209_reactive_loop** (su `update` no recibe `GameContext`; marcar
requiere revisar su bucle). Medidas nuevas: 104 = 38,8 fps · 112 = 12,1 fps · 128 = 49,9 ·
107 = **no arranca** (DT-014).

Pendiente: por cada demo < 50 fps, entrada propia (síntoma + atribución con perfil/secciones
como en la 065/066) y **«scroll brusco»** como categoría de calidad aparte del fps (cadencia
uniforme y sin tirones es gate del `PROCEDIMIENTO` §1; las demos con scroll de la familia
tile/xlimited deben pasar por `check-elements`/band-diff en secuencia antes de declararse
VERIFICADAS). Barrido de cadencia hecho (DT-015): 112/128/202 uniformes; **101 y 104 con
anomalías**.

## DT-014 · Demo 107 (`xlimited_corkscrew`): init de ~45 s (arranca; falta bajar el init)

**Preexistente**: `run-demo` no alcanzaba READY en 40 s (`state=2 InitStarted`); con el engine en
HEAD falla igual; última secuencia en `out/run/107…` del 2026-09-12. Inspección en vivo: el CPU
**progresa** por bucles grandes (`memcpy`/`DIVU`) — init patológicamente largo, no cuelgue.

**Arreglos aplicados (2026-10)**: (1) `build_map_coherent` con contador incremental en vez de
`(x + y) % 97` por celda (80k `__umodsi3` ≈ 12M ciclos) → mapas 36,4k; (2) `pre_scroll` usa
**`scroll_burst_right_px`** en tramos de 24 px (nuevo `XLimitedPlayfield::scroll_burst_right_px`;
equivale a los pasos, HOST-034). **Resultado: READY en ~45 s** (antes: timeout).

**Lo que queda**: la fase **Y** del pre-scroll (256 pasos × 2 playfields de `scroll_down`) —
siguiente paso `burst_down`/`burst_up` en `ScrollEngine` (espejo de `burst_right`) o reducir
`kPreScroll`. Sellos de ciclo del init legibles por canal lateral (`g_init_stamps` en la demo).
Después: medir fps (la espera de `measure-fps` es más corta que el init) y cadencia. Ficha:
`demos/techniques/amiga/playfield/107_xlimited_corkscrew/VALIDATION.md`.

## DT-015 · Cadencias de la familia scroll (barrido 2026-10)

Captura de 6 frames consecutivos (`--sequence-step-frames 6`) + `out/tmp/cadence.mjs` (diff por
bandas entre pares) en la banda central:

| demo | pares (diff %) | veredicto |
|---|---|---|
| 112_xlimited_robocod | 5,5 · 5,5 · 5,4 · 5,5 · 5,5 | **uniforme** (el problema es fps: 12,1) |
| 128_strip_scroller | 23,8 · 23,9 · 23,9 · 23,8 · 23,7 | **uniforme** (49,9 fps) |
| 202_xlimited_dpf | 48,8 · 48,9 · 48,9 · 48,9 · 49,0 | **uniforme** (28,5 fps, 2 campos) |
| 101_ehb_tile_scroll_driver | 30,6 · **50,7** · **0,0** · 30,4 · 30,5 | **anomalía**: un frame repetido + un salto 1,7× |
| 104_tile_scroll_ring_dualpf | **0,0 · 0,0 · 0,0 · 64,9 · 64,8** | **escalonado**: avanza a saltos (38,8 fps) |

Siguiente paso: 101 y 104 — identificar el frame exacto (los nombres `frame_NNN_fNNNN.png` traen
el nº de frame) y perfilarlo; luego visión de secuencia en toda la familia. VALIDATIONs creadas
(101/104/107/112/128; 202 ampliada).

**Barrido ampliado (2026-10, familia restante, settle 2 s):**

| demo | pares | veredicto |
|---|---|---|
| 110_ylimited_shooter | 43,3 · 43,3 · 43,1 · 43,1 · 43,1 | **uniforme** (el rediseño del FG no rompió la cadencia) |
| 203_world_tilemap_xlimited | 25,4 · 25,3 · 25,6 · 25,4 · 25,5 | **uniforme** |
| 205_xy_limited_scroll | 18,5 · 18,6 · 18,7 · 18,7 · 18,7 | **uniforme** |
| 130_app_scroll_ladder | 18,4 · 18,5 · 18,6 · 25,7 · **0,0** | uniforme + evento final (artefacto o fase) |
| 131_app_split_screen | 0,7 · 2,4 · 0,7 · 0,7 · 0,7 | movimiento muy leve en la banda central (medir por franjas del split) |
| 103_tile_scroll_ring | 86,5 · **0,0** · 29,1 · 49,1 · **0,0** | irregular (separar coreografía vs artefacto con ventana mayor) |
| 105_tile_scroll_xyunlimited_dualpf | 46,1 · **0,0** · 46,2 · **0,0** · 46,1 | **alterna 46/0** (periódico; ¿paso a 2 frames?) |
| 132_runtime_geometry / 204_app_strip_scroll | 0,0 ×5 (por intervalo) | **`update` no se ejecuta bajo el runner** (READY ✓, frames=0, pantalla estática; el `render` sí corre) — revisar el bucle del App |

**Conclusiones del barrido completo:** uniformes 112/128/202/110/203/205; 101/104/130 = artefacto
de captura (congelado en el ready-probe frente al swap de `COP1LC`) o fase de ruta; 103/105 con
patrones a caracterizar (ventana mayor); 132/204 con el hook de `update` muerto. VALIDATIONs
creadas para toda la lista.

## DT-016 · Demo 084 (`mf_rotation`): el analyze falla — el cubo sale azul (tono lejano), sin blanco

**Preexistente**: en la regresión completa (`out/regression/20261015-015641` y `…-150014`) la 084
compila y arranca (build ✓ run ✓) pero su `analyze` falla (`| 084_mf_rotation | ok | ok | fail | …
| analyze |`); ya ocurría antes del hilo de sprites/HUD.

**Reproducido (2026-10)**: `bash tools/analyze/analyze-demo.sh demos/techniques/amiga/3d/084_mf_rotation`
→ `FAIL : white=0 < 8`. La captura renderiza (cubo/línea rotando sobre fondo oscuro, marco tenue)
pero **la arista del cubo se pinta en azul** (~color 3) en vez de blanco: `shade_of(m_wz[..])`
debería dar 7 (0xFFF) en las aristas cercanas y todas salen con el tono lejano → el sombreado por
Z no discrimina (`m_wz` — profundidad transformada con `q0`/MiniFloat16) o `draw_line` ignora el
color. El analyze exige `--min-white 8 --min-nonblue 5000` y white=0 lo bloquea.

**Siguiente paso**: en la 084, volcar `m_wz` por arista (telemetría/`Periph` counters ya presentes)
y comparar con `m_scr`; si la profundidad es correcta, revisar `draw_line` (color) del
`playfield::Surface`. Ficha de la demo: `demos/techniques/amiga/3d/084_mf_rotation/README.md`.

## DT-007 · Demo 085 a 16,6 fps (3 fields) y mancha blanca del disco — RESUELTO

`measure-fps 085_copper_plan_scene A500_debug` era **16,62 fps / 3,01 fields / 426.733 ciclos**.
**Atribución por ablación:** sin intents del BOB → **24,99 fps / 2,00 fields / 283.920 ciclos** (el
BOB ≈ **143k**); congelar la fase del cielo (sin `rebuild`) **no** cambiaba nada; la base (plan + 16
intents de cielo + materialize/commit) seguía a **2× presupuesto (~284k)**. El blanco alternante
(1088↔2204) era el **ciclo de paleta del disco** (constante 1088 con fase congelada), no corrupción.

**Causa raíz (perfil por secciones, 2026-10):** `Plan::materialize` costaba **194k para ~85
intenciones** (`sort_lines` 37k + `sort_prio` 33k + `emit` 124k) y el dibujo del disco por CPU
byte a byte en Chip (con contención de bus) otros **~101k**; el resto de `build_frame` ~119k.
La demo re-emitía y re-ordenaba la lista ENTERA cada frame y pintaba el BOB a mano.

**Arreglo:** lista **estática por bitmap** (setup; solo cambian los `BPLxPT`) + **parcheo por
slots** (cielo con `RasterGradientEffect::bind_slots`/`patch_words`; BOB con intents de línea fija
para todo el rango de Y y parcheo del rango actual ∪ anterior) + **disco por Blitter** (borrado
desde fila de ceros con módulo −8 + copia de variante pre-shifteada de 16 offsets de palabra;
bloque de variantes en Chip). Resultado: **49,92 fps / 1,00 field / 142.102 ciclos**, visión OK
sobre secuencia fresca. Informe y lecciones:
`demos/techniques/amiga/copper/085_copper_plan_scene/VALIDATION.md` y
`docs/guides/optimization/ATRIBUCION_DE_CICLOS.md`.

## DT-008 · `profile.mjs` no resuelve `g_eng_prof`

El símbolo vive en `.gnu.linkonce.b._ZN3eng5debug10g_eng_profE` y el `.map` no le da dirección resoluble por el script (`mapSections` solo mira `.text/.rodata/.eh_frame/.data/.bss`), así que el perfil por secciones no arranca. Mientras se arregla (o se instrumentan secciones del bucle XLimited), la atribución de coste se hizo por **ablación** (desactivar un elemento y medir) — método documentado en el informe de la 110.

**Nota (2026-10) — lectura manual que sí funciona**: dirección = base de `.data` (canal lateral `state` → `sections[4]`) + (dirección de enlace de `g_eng_prof` del `.map` − base de enlace de `.data`); se lee con `mem <addr> <len>` del canal lateral. Así se midió la 066 (secciones del update: espera 0.7k / sim+plan+lanzamiento 163k / copper 0.3k).

## DT-013 · Barrido de punteros DMA crudos en APIs — etapas pendientes

Estudio completo y lo corregido (050/051/052 + `make_interleaved_masked_bob`) en
`docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md` §Barrido de punteros DMA crudos. Estado por
etapas (orden por relación valor/riesgo):

1. **Copper `takeover_display`/`install_copper_list` → `ChipView<CopperTag>` — HECHA.** Firmas del
   backend tipadas; accesores `chip_view()` (ListBuilder/SchedulerT), `active_view()`/`inactive_view()`
   (DoubleBuffer/Plan/Scene) y `mem_view()`/`mem_view_chip()` (bloques); 61 demos convertidas (incl.
   variantes `m_copper_views[]`, `build_copper()`, `m_clist[]`, `m_display_view`, `m_scene.active_view()`,
   `m_cu[].mem_view_chip()`); mocks host 069/070/234/240 actualizados; flota 130/130 y suite host 411 OK;
   humo runtime (8 demos: READY + fps de baseline + visión) sin regresión — el barrido es de tipos,
   sin coste en ejecución.
2. Save-under `bob_save_box/restore_box` + `ActorDesc::save` + `SpriteLayer::m_save` → vista tipada
   (rizoma; cambiar los tres juntos).
3. Intenciones de sprite (`sprite_ptr`/`line_data`) → `ChipView<SpriteTag>`.
4. Tiles/mundo (`xlimited_playfield`, `tile_scroll`, `field_controller`, `pattern_fill`).
5. Patrón del gate en `type-tagging.mjs` para firmas nuevas.

## DT-011 · FastBobLayer: hoja de márgenes MÍNIMOS congelaba la demo (126) — RESUELTO

Síntoma: al cablear F5 en la 126 con los pasos **reales** del asset (3, 2 → hoja 22×20) la demo
se congelaba (frame=0). La bisección de geometría (`22×18` ✗ · `22×20` ✗ · `22×32` ✓ ·
`32×18` ✓ · `32×32` ✓) parecía señalar una combinación de ancho/alto.

**Causa raíz real (watchpoint sobre INTENA + log de trazas)**: no era la geometría — era la
**enfermedad del 212**: con el defecto `m_vblank_sleep = true` la espera de frame usa `Wait()`
de exec bajo un SO vivo, y el planificador **reescribe INTENA en cada cambio de tarea,
borrando el VERTB** que arma el engine (INTENAR quedaba en `master|BLIT` sin VERTB) → la tarea
dormida para siempre. La hoja pequeña solo cambiaba el **timing** y hacía 100% reproducible la
carrera que con la hoja grande casi nunca se disparaba. **Fix**: `set_vblank_sleep(false)`
(variante 2 del 212: esperas activas por VPOSR, sin `Wait()`) en el `init` de la 126.

**Resultado**: márgenes **mínimos** (3, 2 → 22×20) + 49.87 fps (1 campo) + visión OK (sin
rastros ni huecos). Lección para demos bajo el runner: cualquier demo que tome el display y
use `run_frames` con `Wait()` es sensible a este timing; el setup robusto es
`set_vblank_sleep(false)` (y `takeover_system` si se necesita `Wait()` de verdad).

## DT-012 · FastBobLayer: el camino de DEGRADACIÓN pintaba basura en el borde izquierdo (126) — RESUELTO

Al cablear el camino lento de la capa (hoja cookie-cut sin padding + `set_slow_sheet`) y
**dispararlo** en la 126 (velocidad > pad en una ventana determinista) aparecían: (1) una columna
de basura en el borde izquierdo; (2) en alguna instancia, congelación.

**Causa raíz (2026-10):** en `bob_draw` (`engine/include/eng/graphics/bob.hpp`), hoja **planar**
con **máscara separada** sobre destino **interleaved** (el caso de la 126: PF1 DPF) ejecutaba UN
blit de `height*planos` filas — el truco válido para la imagen sola (el orden físico de filas
coincide). Pero la **máscara tiene `height` filas, no `height*planos`**: el walk de la máscara se
salía de su plano y leía Chip ajena como máscara (planos 1+ con máscara basura).

**Arreglo:** ese caso va **por plano** (misma máscara en cada uno; su walk usa `src_mod`, el
stride real de la máscara) con el paso de fila FÍSICA del destino (`row_bytes*planos − words*2`);
el blit único se conserva para imagen sola (sin regresión de los fast paths). **HOST-439 T4b**
nuevo (hoja planar + máscara separada → target interleaved, con canario `0xFFFF` tras la máscara:
con el bug el interior sale con máscara=1 y falla; con el arreglo pasa — sensibilidad verificada
revirtiendo `flat`). El ejecutor SW del test generalizó su modelo OOB (paso de fila =
`words*2 + DMOD`; en interleaved los planos comparten bloque).

**Validación en la 126:** disparo reactivado (bob0 a 5 px/frame en [112,128) de cada 128 frames —
el camino lento ya es parte del showcase); **49,92 fps / 1,00 field / 1000 frames**; franja
izquierda idéntica dentro/fuera de la ventana (sin columna de basura); visión OK (3 frames,
bobs completos, sin artefactos).

## DT-010 · Deriva de API en demos: gate «compilar la flota» — RESUELTO (2026-10)

Gate nuevo: `tools/build/build-all-demos.sh [-j N]` compila **todas** las demos (solo compilar) y falla si alguna no compila; deja un resumen en `out/build-all-demos/summary.txt` y el log concreto por demo. El contador de versión de `build-demo.sh` pasó a **incremento atómico** (lock `mkdir`) porque en paralelo el contador retrocedía.

**Primer barrido: 101 OK / 29 FAIL** — deriva acumulada que ningún gate veía. Arregladas las 29 (fleet en **130/130 OK**):

- **Cabeceras del engine obsoletas**: `GameAudio::init(MemorySystem&)` (→ `MemoryManager&`; 6 demos), `palette_transition`/`copper_chunky` con `[[nodiscard]]` ignorado (4 demos), `acp1.hpp` con división de 64 bits (`__udivdi3`, libcall prohibida; 2 demos).
- **`BlitPtr` tipado**: `BlitJob` brace-init con punteros crudos → `BlitPtr::from_storage` (050/051/052/103/104/203/210).
- **`ChipView` en el tipo**: `SpriteConfig::data`/`SpriteManager` (dk_port, dk_port_xl, 053), `HwSpritePlacement`/`SpriteConfig` con campos nuevos (107), orden de argumentos de `StripScrollLayer::begin` (128).
- **`nodiscard`** (205/213/215) y **`atexit`** de un `static` local con destructor bajo `-nostdlib` (123), **`g_eng_prof` duplicado** (116 usa el bloque del engine).

Pendiente del gate: integrarlo opcionalmente en la pasada de flota (hoy es on-demand por coste).
