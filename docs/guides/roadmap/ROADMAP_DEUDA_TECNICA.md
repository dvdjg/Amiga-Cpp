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

**Actualización (2026-10):** 085 y 215 resueltas (DT-007; 215 = `mark_frame` ausente); barrido `mark_frame` aplicado a 34 demos que no marcaban frame (la telemetría es norma; sin ella `measure-fps` da 0). Queda **209_reactive_loop** (su `update` no recibe `GameContext`; marcar requiere revisar su bucle). Medidas nuevas: 104 = 38,8 fps · 112 = 12,1 fps · 128 = 49,9 · 107 = **no arranca** (DT-014).

Pendiente: por cada demo < 50 fps, entrada propia (síntoma + atribución con perfil/secciones como en la 065/066) y **«scroll brusco»** como categoría de calidad aparte del fps (cadencia uniforme y sin tirones es gate del `PROCEDIMIENTO` §1; las demos con scroll de la familia tile/xlimited deben pasar por `check-elements`/band-diff en secuencia antes de declararse VERIFICADAS). Barrido de cadencia hecho (DT-015): 112/128/202 uniformes; **101 y 104 con anomalías**.

## DT-014 · Demo 107 (`xlimited_corkscrew`): init de ~45 s (arranca; falta bajar el init)

**Preexistente**: `run-demo` no alcanzaba READY en 40 s (`state=2 InitStarted`); con el engine en HEAD falla igual; última secuencia en `out/run/107…` del 2026-09-12. Inspección en vivo: el CPU
**progresa** por bucles grandes (`memcpy`/`DIVU`) — init patológicamente largo, no cuelgue.

**Arreglos aplicados (2026-10)**: (1) `build_map_coherent` con contador incremental en vez de `(x + y) % 97` por celda (80k `__umodsi3` ≈ 12M ciclos) → mapas 36,4k; (2) `pre_scroll` usa
**`scroll_burst_right_px`** en tramos de 24 px (nuevo `XLimitedPlayfield::scroll_burst_right_px`; equivale a los pasos, HOST-034). **Resultado: READY en ~45 s** (antes: timeout).

**Lo que queda**: la fase **Y** del pre-scroll (256 pasos × 2 playfields de `scroll_down`) — siguiente paso `burst_down`/`burst_up` en `ScrollEngine` (espejo de `burst_right`) o reducir `kPreScroll`. Sellos de ciclo del init legibles por canal lateral (`g_init_stamps` en la demo). Después: medir fps (la espera de `measure-fps` es más corta que el init) y cadencia. Ficha: `demos/techniques/amiga/playfield/107_xlimited_corkscrew/VALIDATION.md`.

## DT-015 · Cadencias de la familia scroll (barrido 2026-10)

Captura de 6 frames consecutivos (`--sequence-step-frames 6`) + `out/tmp/cadence.mjs` (diff por bandas entre pares) en la banda central:

| demo | pares (diff %) | veredicto |
|---|---|---|
| 112_xlimited_robocod | 5,5 · 5,5 · 5,4 · 5,5 · 5,5 | **uniforme** (el problema es fps: 12,1) |
| 128_strip_scroller | 23,8 · 23,9 · 23,9 · 23,8 · 23,7 | **uniforme** (49,9 fps) |
| 202_xlimited_dpf | 48,8 · 48,9 · 48,9 · 48,9 · 49,0 | **uniforme** (28,5 fps, 2 campos) |
| 101_ehb_tile_scroll_driver | 30,6 · **50,7** · **0,0** · 30,4 · 30,5 | **anomalía**: un frame repetido + un salto 1,7× |
| 104_tile_scroll_ring_dualpf | **0,0 · 0,0 · 0,0 · 64,9 · 64,8** | **escalonado**: avanza a saltos (38,8 fps) |

Siguiente paso: 101 y 104 — identificar el frame exacto (los nombres `frame_NNN_fNNNN.png` traen el nº de frame) y perfilarlo; luego visión de secuencia en toda la familia. VALIDATIONs creadas (101/104/107/112/128; 202 ampliada).

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

**Conclusiones del barrido completo:** uniformes 112/128/202/110/203/205; 101/104/130 = artefacto de captura (congelado en el ready-probe frente al swap de `COP1LC`) o fase de ruta; 103/105 con patrones a caracterizar (ventana mayor); 132/204 con el hook de `update` muerto. VALIDATIONs creadas para toda la lista.

## DT-016 · Demo 084 (`mf_rotation`): analyze — cubo sin blanco y trazado por CPU — RESUELTO (2026-10)

**Preexistente**: en la regresión completa la 084 fallaba su `analyze` (`white=0 < 8`).

**Diagnóstico (con simulación host de la matemática, coincidente con el emulador: `m_wz` a ±54–57 en el frame 120)**: la matemática (MiniFloat16 + `transform` 4.12) es correcta; el problema era el **trazado por CPU** (`draw_line` sin `FramePlan` → raster CPU): ~5,2M ciclos/frame (1,35 fps) y las aristas claras no llegaban a pintarse (el camino CPU dejaba el cubo casi invisible).

**Arreglo**: el trazado va por **Blitter** (patrón 077/079: `scene.draw_target(&plan)` + `dt.line(...)` + `execute_frame_plan`) en `update` (no en `render`: el breakpoint del ready-probe en el vblank congelaba la captura a medio dibujar). **Analyze: OK (`white=174`)**; 2,04 fps (25 campos).

**Lo que queda**: el fps sigue bajo (borrado por CPU — `add_line` con color 0 no se encola — y coste por job del Blitter). Ficha en el listado de calidad: `docs/guides/roadmap/DEMOS_A_ELIMINAR_O_MEJORAR.md`. La familia 077/078 (mismas líneas CPU) y 079 (Blitter, coste/job) queda en el mismo listado.

## DT-017 · Demos que vuelven al SO (crash) tras READY: contador de frame congelado

**Síntoma**: la demo alcanza READY y renderiza (la captura del arranque es correcta), pero a los pocos frames el contador de `g_eng_run_status.frame` se congela y el PC acaba en la ROM del KickStart (`0xFCxxxx`): la demo **vuelve al SO**. `measure-fps` lo reporta como 0 fps / ~1001,6 campos/frame (el contador no avanza en la ventana); el `analyze` de la regresión **no lo ve** (captura antes del crash).

**Verificado**: `221_sprite_palette_split` (se congela en el frame 39; pc=0x00FC0F90, sr con S=1) — **preexistente**: recompilada con el engine anterior a los fixes de sprites (`b22013a2`) el crash es idéntico. Misma firma en `130_app_scroll_ladder`, `132_runtime_geometry`, `204_app_strip_scroll`, `214_floppy_raw`, `071_mixer_four_voices`.

**Siguiente paso**: capturar el marco de excepción completo (SSP) en el momento del crash (breakpoint en el vector de excepción, `winuae_postmortem_capture`) y localizar la rutina; el crash a frame fijo (~39) apunta a un estado de animación concreto (pistón/paleta en 221). Listado de calidad asociado: `docs/guides/roadmap/DEMOS_A_ELIMINAR_O_MEJORAR.md`.

## DT-007 · Demo 085 a 16,6 fps (3 fields) y mancha blanca del disco — RESUELTO

`measure-fps 085_copper_plan_scene A500_debug` era **16,62 fps / 3,01 fields / 426.733 ciclos**.
**Atribución por ablación:** sin intents del BOB → **24,99 fps / 2,00 fields / 283.920 ciclos** (el BOB ≈ **143k**); congelar la fase del cielo (sin `rebuild`) **no** cambiaba nada; la base (plan + 16 intents de cielo + materialize/commit) seguía a **2× presupuesto (~284k)**. El blanco alternante (1088↔2204) era el **ciclo de paleta del disco** (constante 1088 con fase congelada), no corrupción.

**Causa raíz (perfil por secciones, 2026-10):** `Plan::materialize` costaba **194k para ~85 intenciones** (`sort_lines` 37k + `sort_prio` 33k + `emit` 124k) y el dibujo del disco por CPU byte a byte en Chip (con contención de bus) otros **~101k**; el resto de `build_frame` ~119k. La demo re-emitía y re-ordenaba la lista ENTERA cada frame y pintaba el BOB a mano.

**Arreglo:** lista **estática por bitmap** (setup; solo cambian los `BPLxPT`) + **parcheo por slots** (cielo con `RasterGradientEffect::bind_slots`/`patch_words`; BOB con intents de línea fija para todo el rango de Y y parcheo del rango actual ∪ anterior) + **disco por Blitter** (borrado desde fila de ceros con módulo −8 + copia de variante pre-shifteada de 16 offsets de palabra; bloque de variantes en Chip). Resultado: **49,87 fps / 1,00 field / 142.102 ciclos**, visión OK sobre secuencia fresca. Informe y lecciones: `demos/techniques/amiga/copper/085_copper_plan_scene/VALIDATION.md` y `docs/guides/optimization/ATRIBUCION_DE_CICLOS.md`.

## DT-008 · `profile.mjs` no resuelve `g_eng_prof`

El símbolo vive en `.gnu.linkonce.b._ZN3eng5debug10g_eng_profE` y el `.map` no le da dirección resoluble por el script (`mapSections` solo mira `.text/.rodata/.eh_frame/.data/.bss`), así que el perfil por secciones no arranca. Mientras se arregla (o se instrumentan secciones del bucle XLimited), la atribución de coste se hizo por **ablación** (desactivar un elemento y medir) — método documentado en el informe de la 110.

**Nota (2026-10) — lectura manual que sí funciona**: dirección = base de `.data` (canal lateral `state` → `sections[4]`) + (dirección de enlace de `g_eng_prof` del `.map` − base de enlace de `.data`); se lee con `mem <addr> <len>` del canal lateral. Así se midió la 066 (secciones del update: espera 0.7k / sim+plan+lanzamiento 163k / copper 0.3k).

## DT-013 · Barrido de punteros DMA crudos en APIs — etapas pendientes

Estudio completo y lo corregido (050/051/052 + `make_interleaved_masked_bob`) en `docs/engine/architecture/INTERNAL_TYPE_SYSTEM.md` §Barrido de punteros DMA crudos. Estado por etapas (orden por relación valor/riesgo):

1. **Copper `takeover_display`/`install_copper_list` → `ChipView<CopperTag>` — HECHA.** Firmas del backend tipadas; accesores `chip_view()` (ListBuilder/SchedulerT), `active_view()`/`inactive_view()` (DoubleBuffer/Plan/Scene) y `mem_view()`/`mem_view_chip()` (bloques); 61 demos convertidas (incl. variantes `m_copper_views[]`, `build_copper()`, `m_clist[]`, `m_display_view`, `m_scene.active_view()`, `m_cu[].mem_view_chip()`); mocks host 069/070/234/240 actualizados; flota 130/130 y suite host 411 OK; humo runtime (8 demos: READY + fps de baseline + visión) sin regresión — el barrido es de tipos, sin coste en ejecución.
2. Save-under `bob_save_box/restore_box` + `ActorDesc::save` + `SpriteLayer::m_save` → vista tipada (rizoma; cambiar los tres juntos).
3. Intenciones de sprite (`sprite_ptr`/`line_data`) → `ChipView<SpriteTag>`.
4. Tiles/mundo (`xlimited_playfield`, `tile_scroll`, `field_controller`, `pattern_fill`).
5. Patrón del gate en `type-tagging.mjs` para firmas nuevas.

## DT-011 · FastBobLayer: hoja de márgenes MÍNIMOS congelaba la demo (126) — RESUELTO

Síntoma: al cablear F5 en la 126 con los pasos **reales** del asset (3, 2 → hoja 22×20) la demo se congelaba (frame=0). La bisección de geometría (`22×18` ✗ · `22×20` ✗ · `22×32` ✓ · `32×18` ✓ · `32×32` ✓) parecía señalar una combinación de ancho/alto.

**Causa raíz real (watchpoint sobre INTENA + log de trazas)**: no era la geometría — era la
**enfermedad del 212**: con el defecto `m_vblank_sleep = true` la espera de frame usa `Wait()` de exec bajo un SO vivo, y el planificador **reescribe INTENA en cada cambio de tarea, borrando el VERTB** que arma el engine (INTENAR quedaba en `master|BLIT` sin VERTB) → la tarea dormida para siempre. La hoja pequeña solo cambiaba el **timing** y hacía 100% reproducible la carrera que con la hoja grande casi nunca se disparaba. **Fix**: `set_vblank_sleep(false)` (variante 2 del 212: esperas activas por VPOSR, sin `Wait()`) en el `init` de la 126.

**Resultado**: márgenes **mínimos** (3, 2 → 22×20) + 49.87 fps (1 campo) + visión OK (sin rastros ni huecos). Lección para demos bajo el runner: cualquier demo que tome el display y use `run_frames` con `Wait()` es sensible a este timing; el setup robusto es `set_vblank_sleep(false)` (y `takeover_system` si se necesita `Wait()` de verdad).

## DT-012 · FastBobLayer: el camino de DEGRADACIÓN pintaba basura en el borde izquierdo (126) — RESUELTO

Al cablear el camino lento de la capa (hoja cookie-cut sin padding + `set_slow_sheet`) y
**dispararlo** en la 126 (velocidad > pad en una ventana determinista) aparecían: (1) una columna de basura en el borde izquierdo; (2) en alguna instancia, congelación.

**Causa raíz (2026-10):** en `bob_draw` (`engine/include/eng/graphics/bob.hpp`), hoja **planar** con **máscara separada** sobre destino **interleaved** (el caso de la 126: PF1 DPF) ejecutaba UN blit de `height*planos` filas — el truco válido para la imagen sola (el orden físico de filas coincide). Pero la **máscara tiene `height` filas, no `height*planos`**: el walk de la máscara se salía de su plano y leía Chip ajena como máscara (planos 1+ con máscara basura).

**Arreglo:** ese caso va **por plano** (misma máscara en cada uno; su walk usa `src_mod`, el stride real de la máscara) con el paso de fila FÍSICA del destino (`row_bytes*planos − words*2`); el blit único se conserva para imagen sola (sin regresión de los fast paths). **HOST-439 T4b** nuevo (hoja planar + máscara separada → target interleaved, con canario `0xFFFF` tras la máscara: con el bug el interior sale con máscara=1 y falla; con el arreglo pasa — sensibilidad verificada revirtiendo `flat`). El ejecutor SW del test generalizó su modelo OOB (paso de fila = `words*2 + DMOD`; en interleaved los planos comparten bloque).

**Validación en la 126:** disparo reactivado (bob0 a 5 px/frame en [112,128) de cada 128 frames — el camino lento ya es parte del showcase); **49,87 fps / 1,00 field / 1000 frames**; franja izquierda idéntica dentro/fuera de la ventana (sin columna de basura); visión OK (3 frames, bobs completos, sin artefactos).

## DT-010 · Deriva de API en demos: gate «compilar la flota» — RESUELTO (2026-10)

Gate nuevo: `tools/build/build-all-demos.sh [-j N]` compila **todas** las demos (solo compilar) y falla si alguna no compila; deja un resumen en `out/build-all-demos/summary.txt` y el log concreto por demo. El contador de versión de `build-demo.sh` pasó a **incremento atómico** (lock `mkdir`) porque en paralelo el contador retrocedía.

**Primer barrido: 101 OK / 29 FAIL** — deriva acumulada que ningún gate veía. Arregladas las 29 (fleet en **130/130 OK**):

- **Cabeceras del engine obsoletas**: `GameAudio::init(MemorySystem&)` (→ `MemoryManager&`; 6 demos), `palette_transition`/`copper_chunky` con `[[nodiscard]]` ignorado (4 demos), `acp1.hpp` con división de 64 bits (`__udivdi3`, libcall prohibida; 2 demos).
- **`BlitPtr` tipado**: `BlitJob` brace-init con punteros crudos → `BlitPtr::from_storage` (050/051/052/103/104/203/210).
- **`ChipView` en el tipo**: `SpriteConfig::data`/`SpriteManager` (dk_port, dk_port_xl, 053), `HwSpritePlacement`/`SpriteConfig` con campos nuevos (107), orden de argumentos de `StripScrollLayer::begin` (128).
- **`nodiscard`** (205/213/215) y **`atexit`** de un `static` local con destructor bajo `-nostdlib` (123), **`g_eng_prof` duplicado** (116 usa el bloque del engine).

Pendiente del gate: integrarlo opcionalmente en la pasada de flota (hoy es on-demand por coste).

## DT-018 · m68k: `__mulsi3` a -O1 con operandos de 16 bits e ICE -O0 en `Bayer::threshold8`

**Síntoma (medido al portar `corner_tiles.hpp`, 2026-10):** con el cruce m68k (GCC 15.1.0), `nm` del objeto expone `U __mulsi3` por multiplicaciones cuyos operandos **caben en 16 bits**; a `-O2` (el build de las demos) el backend las pliega a `mulu.w`/`muls.w`, pero a `-O1` (build de la regresión) siguen bajando a libcall. Reproducción mínima: `u32(a) * b` con `a,b` u16 en una función `noinline` → `jsr __mulsi3` a `-O1`. La demo 119 debug ya llamaba `__mulsi3` (6 sitios) y `__udivsi3` (3) desde antes: el enlace funciona porque `support/gcc8_a_support.s` define las rutinas, pero cada llamada cuesta un salto + ~4 `mulu.w`.

**Mitigación aplicada (final):** los productos 16×16 del dominio van por el vocabulario del engine — `eng::math::mulu16`/`mul_wide` (`arith<m68k>`: asm `mulu`/`muls` con guard `if consteval`, portable y bit-exacto en `consteval`) y la tabla ROM `kSquares` para los cuadrados del `stamp_disc`. Con eso el TU completo de `noise_tile.hpp` + `corner_tiles.hpp` + `layered_map.hpp` queda con **0 libcalls (`nm`) a `-O1` y `-O2`**, y `HOST-443`/`HOST-444`/`HOST-445` confirman que no cambió ningún valor. Regla práctica: modelar los productos como HImode (s16/u16) o tabla — un producto con tipos `int`/`u32` puede degradar a libcall aunque el rango sea diminuto.

**Segundo hallazgo (ICE):** un TU con una sola llamada a `Bayer::threshold8` (p. ej. `extern "C" u8 probe(u16 x, u16 y) { return eng::gfx::proc::Bayer::threshold8(x, y); }`) da `internal compiler error: in dwarf2out_frame_debug_adjust_cfa, at dwarf2cfi.cc:1348` a `-O0`
**también sin lambdas y con `-g0`** (el trigger documentado hasta ahora eran lambdas locales; aquí no hay). Afecta solo a los builds `--o0` de depuración interactiva; `-O1`/`-O2` compilan. Registrado aquí; sin causa raíz en el fuente (bug de backend/CFI).

## DT-019 · `write_ppm` de `HOST-442` no termina con imágenes > 65535 px — RESUELTO

El helper `write_ppm` del test `tests/host/graphics/442_proc_bitmaps` usa índice `u16` en el bucle (`for (u16 i = 0; i < u16(w) * h; ++i)`): con `w*h > 65535` el índice desborda y el bucle no termina (el dump de `map.ppm` de 320×224=71680 px cuelga si algún día se conecta por ese camino; hoy el mapa se escribe con bucles de fila y no lo toca). `HOST-448` usa `u32` en su copia del helper (funcionó y se detectó ahí). **Arreglo:** cambiar el índice a `u32` en 442.

## DT-020 · `cast-audit --check` en rojo sobre HEAD (baseline desactualizado)

`node tools/analyze/cast-audit.mjs --check` falla en HEAD sin cambios locales: `field/surface.hpp` tiene 51 `static_cast` (baseline 35) y `graphics/planar_band.hpp` 3 `reinterpret+const` (baseline 0). Son ficheros **no tocados** por el hilo de rachas de Blitter (2026-10-16): o el baseline quedó corto tras cambios previos, o hay casts reales que migrar a tipos. **Resuelto (2026-10-16)**: revisados ambos: los 3 de `planar_band.hpp` son frontera documentada (vistas de palabra sobre buffers planares alineados) y los 51 de `surface.hpp` son estrechamientos de rects/texto con rango acotado; aceptados en `tools/check/casts-baseline.txt` (entradas actualizadas). El gate queda en verde.

**Reapertura parcial (2026-10-18, hilo HUD).** El gate vuelve a rojo con offenders de ficheros **ajenos al hilo HUD**: `graphics/planar_patch.hpp` (5), `rpg/combat.hpp` (8), `rpg/quest.hpp` (10), `rpg/sim_bridge.hpp` (1) — de la capa RPG (`e312bbbf`) —, `retro/wireframe.hpp` (3, `606e487d`) y `graphics/pan_window.hpp` (2, en vuelo, sin commitear). El hilo HUD actualizó en la misma pasada sus propias entradas (`hud/model.hpp` 29, `hud/region.hpp` 3, `hud/strip.hpp` 5 — nuevos desde `cfdb7a25`) y `ui/widgets.hpp` (33 → 36, dispatch de `UiAction` en `cfdb7a25`); los casts ajenos se revisaron **uno a uno**: todos son estrechamientos de rango acotado (`u16`/`s16`/`u8`/`usize`), no fronteras, así que se aceptaron en el baseline para desbloquear el barrido de flota (gate en verde). **Pendiente:** cada hilo reduce los suyos cuando toque ese código (p. ej. `planar_patch` puede tipar el índice con `constexpr`); el baseline solo puede bajar.

## DT-021 · Demo 213: `A500_release` no alcanza READY (init, reserva Chip)

`A500_release` de la 213 falla en init con `detail=0x06CBF005` (`mark_mem_failed` del grupo sprite/música, banco Chip con ~191 KB libres). Reproducible en el engine sin los cambios del turno. Detalle y evidencia: `docs/debugging/investigaciones/213_release-strict-aliasing.md` §Reapertura.

**Causa raíz y fix (2026-10-16).** El Chip queda fragmentado por la carga del ejecutable: el mayor bloque al arrancar el juego es ~236 KiB en release (vs ~428 KiB en debug), y `game_memory_fit_available` restaba un headroom **fijo** de 128 KiB → pool de ~108 KiB para una necesidad de ~110 KiB (escena 50 KiB + assets img 50 KiB + bob/mod/copper). Fix:
**el headroom de Chip escala con el bloque** (1/4, suelo 64 KiB, tope 128 KiB) en `memory_profile.hpp`; con 236 KiB el pool pasa a 172 KiB y la reserva de assets ya no da `NoSpace` (HOST-394 cubre el caso). **Pendiente**: con el pool arreglado la release avanza y queda **colgada en init** (`state=InitStarted`, PC estable en `MemBank::configure`, `debuggerState=2`) — verificado que **no** es el fix de espera del Blitter (persiste con `amiga_blitter.cpp` sin él). Siguiente paso: adjuntar GDB con `WINUAE_GDB_PERSIST_LISTENER=1` y hacer step/backtrace del cuelgue (la herramienta está en reparación por DT-023), y probar la hipótesis de **stack** (128 KiB del runner) subiéndolo a 256 KiB.

## DT-022 · Blitter: plano 0 perdido al mezclar copias y racha cookie-cut en un `FramePlan`

Síntoma: en la demo 121 el actor pierde el **plano 0** de forma intermitente y posicional cuando el plan mezcla 5 `CopyRect` (restaurador) y una racha de 5 `MaskedBobCookieCut` (actor); invisible si el plano 0 del fondo vale 1. Evidencia: `detail=0x1F2102` (auto-chequeo, sub=0, body=30) y `detail=0x1E01252F` (guarda por frame, x≈260, body=30 ≥3 frames). Actor-only nunca falla; dos `execute_frame_plan` separados = 6/6 limpios. Detalle: `docs/debugging/investigaciones/121-actor-plano-0-y-carencias-blits.md`. Acción: test host de la secuencia mixta de registros (extiende HOST-451) y, con repro, fix del motor; mientras, la demo usa planes separados. Relacionado: el contrato de sincronía/verificación de lectura y el validador de geometría de anillo propuestos en el informe.

**Motor corregido (2026-10-16) — contrato de escritura con el Blitter ocupado.** El mecanismo encaja con la fuente primaria: el AHRM prohíbe escribir registros del Blitter con un blit en curso («Multitasking and the Blitter») y WinUAE reinicia el blitter si se escribe `BLTSIZE` con uno activo (`blitter.cpp:2036/2058/2101`; ficha `docs/reference/emulators/winuae/blitter-busy-writes.md`). El plan encadenaba jobs sin esperar (solo «funcionaba» por timing del bus) → en un burst tras un blit largo se pisa el estado del que corre. Fix: `submit_blit_job` espera `BBUSY==0` antes de programar cada job (las rachas `BlobBatch` ya esperaban por objeto). Coste medido en la 213: nulo dentro del ruido (sync 32,30 vs 32,20–32,32 fps; async 27,41 vs 27,58). Reproducción en la demo pendiente de cierre: el A/B local con plan único quedó **contaminado por DT-023** (instancias huérfanas/step-capture); la verificación limpia es la herramienta golden de la 121 (`K121_GOLDEN_ACTORS` + `tools/analyze/verify-121-actor-golden.mjs`). Queda: re-ejecutar el plan único con esa verificación y, si sale limpio, retirar la mitigación de dos planes de la demo.


**Verificación en la demo 121 (2026-10-16)**: con el contrato en el motor, la demo volvió al **plan único** por frame (restaurador + actor juntos; `K121_TWO_PLAN_SPLIT` queda como A/B): 7/7 arranques limpios con telemetría viva y medida estable (47,67 fps / 148,8k ciclos, igual que con dos planes: el coste era espera, no trabajo extra). El banco golden (`K121_GOLDEN_ACTORS` + `verify-121-actor-golden.mjs`) pasa 10/16 subposiciones exactas en multi-plan con máscaras por slot (el resto y ~127 px de colateral quedan en el banco como anomalía residual, ver el informe 121).

## DT-023 · Herramientas: capturas de secuencia a mitad de frame y `--config` ignorado

En `tools/run/run-demo.ts`: (a) `--sequence-step-frames` pausa en el `ready-probe` y captura estados sin los blits del frame reflejados (actor ausente o con motas) — indujo diagnóstico falso de render en la 121; **re-verificado 2026-10-16**: 20/20 frames del step-capture de la 121 con el actor ausente en posiciones donde la captura simple sí lo muestra (x=78 bajo copa es legítimo, x=56..75 no lo es) → el stop del probe sirve un estado previo al dibujo del frame; (b) ~~`--config <id>` se ignora silenciosamente~~ (**corregido en el análisis del 2026-10-16: `--config` funciona; escribe capturas/report en `out/run/<demo>/<CONFIG_ID>/` y leer el directorio `A500_debug` indujo a error — documentar el directorio por config y avisar en consola al forzar `--config`**); (c) el runner a veces termina con `Process exited with code null` tras READY sin escribir report — y una instancia huérfana de un run anterior puede quedar escuchando en los puertos y servir su estado VIEJO al siguiente run (fallos idénticos repetidos con el mismo `detail`, observado 2026-10-16: `0xA1D2108` en arranques consecutivos; 9/10 limpios tras higiene de procesos antes de cada run). Detalle: `docs/debugging/investigaciones/121-actor-plano-0-y-carencias-blits.md` §Carencias de las herramientas. Acción: capturar tras `wait_blitter`/frame completo, hacer efectivo `--config`, y robustecer el cierre del runner. **Cierre del runner, parcial (2026-10-17):** el step-capture quita el breakpoint con la CPU parada (`pause → clear → continue`; antes era una carrera de GDB y el probe podía quedar armado) y el screenshot final cae al monitor GDB si el canal lateral deja la petición `queued` sin servir tras la sesión de breakpoints (ejercitado por la demo 400; detalle en la ficha 400 §6).

**(d) Captura congelada (2026-10-17, en curso)**: el `screenshot` del monitor GDB de este fork congela el subsistema de vídeo tras la primera captura: `screenshot.png` y toda la secuencia salían byte a byte idénticas (mismo md5, incluso el contador del HUD) aunque la demo avanzara (el `runstatus` del canal lateral sí progresa). Mitigación aplicada en el runner: las capturas (principal y secuencia) usan la **vía del canal lateral** (`screenshot <ruta>` — la acción corre en el hilo de emulación) con el monitor como reserva y pausa+continue. En este entorno automatizado (ventana no compuesta) el refresco de vídeo sigue quedando parado a los pocos frames del arranque, así que la secuencia con **movimiento** sigue SIN VERIFICAR hasta probarla con la ventana visible o revisar el fork (el bitmap SÍ avanza: la guarda por frame de la 121 lo lee cada frame). Nota de alcance: las capturas actuales (actor visible en su columna) son válidas como frame; no lo son como evidencia de dinámica.

## DT-024 · Migración de la demo 121 al sistema de actores (cierre de §14.8)

**Estado (2026-10-16)**: puntos 1 y 2 **HECHOS**: `pack_interleaved_pair` (`blit_job.hpp`) cocina la hoja interleaved con guarda (subpíxeles 0..15 exactos en ambas mitades del byte, HOST-453) y el save-under del actor ya soporta destinos interleaved (guard levantado en `actor_store.hpp`; restauración byte a byte verificada sobre el anillo, HOST-453). Queda el punto 3: migrar la demo 121 y adaptar su chequeo/golden.

**Punto 3 (A/B verificado, 2026-10-16)**: la demo compila ambos caminos con `K121_ACTOR_SYSTEM` (A/B, igual que `K121_SINGLE_PLAN`): hoja pack con guarda + `ActorStore`/`Animation` + `SaveUnder` + puente, sin bandas ni ventana propias. Verificado en hardware: **3/3 arranques limpios con la guarda por frame viva**, medida **47,87 fps / 148,2k ciclos** (equivalente al camino propio) y captura estática determinista con los **552 px del cuerpo** exactos (x=200, columna limpia). La cocina de la máscara muestrea las copas en la columna REAL de cada píxel (`x+b`) con composición por shift — error de ±subpíxel que la guarda cazó en x=86 y queda documentado en el código. Falta para cerrar: flip por defecto (retirar el camino propio) y adaptar el **golden** de 16 subposiciones al sistema (el oráculo JS vale tal cual: las posiciones esperadas no cambian).

**Avance (2026-10-17)**: el init del camino del sistema quedó alineado con el update (ambos en coordenadas de anillo: `d.x = kActorMinX + kRingGuardX`; el init no lo sumaba y el actor se dibujaba 16 px a la izquierda de lo que lee la guarda — `detail 0xB2133`). Con eso, el actor del sistema **se ve en captura** (552 px del cuerpo blanco en su columna exacta, verificado con bbox independiente y `VISION.md` regenerado: el modelo ve al personaje). Añadido el modo `K121_ACTOR_SEAM` (actor cruzando la costura del anillo): auto-chequeo por frame leyendo del bitmap la parte visible (anillo `kDisplayH−2`) y la ENVUELTA (anillo 8, guarda) — pasa en hardware cada frame (con cámara fija el cruce queda oculto en las guardas por diseño; la evidencia es la lectura del bitmap).

**CIERRE (2026-10-17)**: **DT-024 cerrado**. (a) **Flip por defecto**: el camino del SISTEMA es el default de la demo (el camino propio queda tras `K121_ACTOR_LEGACY` como referencia del A/B; ambos arrancan limpios). (b) **Golden del sistema**: 16 actores del `ActorStore` (pack compartido — en columna limpia la máscara es la silueta —, política `None`, sin clip: la guarda del par hace no-op el desborde de las últimas columnas) → **16/16 subposiciones EXACTAS y 0 px de daño colateral** con el oráculo `tools/analyze/verify-121-actor-golden.mjs` (que ahora ancla el origen de la captura por el blanco identidad del índice 31 — el borde de ventana varía por run — y excluye la unión de patrones en ventanas solapadas). El golden del camino propio (10/16 + colateral) queda como referencia histórica. Pendiente solo de herramientas: la secuencia con movimiento (DT-023d).

El puente actor ↔ anillo corkscrew ya existe (`engine/include/eng/field/actor_target.hpp`, HOST-453) y `bob_draw` aplica el subpíxel con el barrel (`source_shift = x & 15`, una hoja por arte). Falta para migrar la 121 (`ActorStore` + `Animation` + `actor_emit` sobre el anillo):

1. **Pack interleaved con guarda de 2 palabras**: la hoja pair debe ser `[img w0][img w1=0][mask w0][mask w1=0]` por fila y plano del destino; con 1 palabra por fila el shift arrastra bits de la máscara (en el blitter real AFWM gana sobre ALWM cuando solo hay una palabra; el test HOST-453 lo fija como borde). Documentar la receta en `OBJECT_SYSTEM.md` al migrar.
2. **Save-under en destinos interleaved**: `BackgroundPolicy::SaveUnder` exige hoy layout `Planar` (`actor_store.hpp` devuelve `Full`); la 121 restaura vía band del carril (`add_world_window`). Implementar save/restore interleaved (`bob_save_box`/`restore_box` con dmod = `row_bytes·planos − words·2`) con test host, o declarar una política de restauración por banda en el emisor.
3. **Migrar la demo** y adaptar su auto-chequeo/golden (`K121_GOLDEN_ACTORS` pasa a ejercitar el sistema real — el gate visual que pide §14.8) + `VALIDATION.md`.

Referencia: `docs/engine/architecture/OBJECT_SYSTEM.md` §2/§14.8; evidencia del puente en HOST-453.

## DT-025 — Coste real por job del plan de Blitter y del HUD por texto (demostración RPG 001)

**Origen:** campaña de puesta a punto de la demo `demos/features/rpg/amiga/001_rpg_dungeon` (capa RPG, 2026-10-17). Todo con `A500_debug`, `measure-fps` y capturas de intervalo (`run-report.json`), ablación por etapas (una variable por build vía `build.args`).

**Síntoma y medidas:**

1. **Cada job del plan de Blitter paga una sincronización completa con el Blitter.** Una escena de sala (~20 rectángulos con `s.fill`/`s.line`/`s.fill_box` por el plan) medida a **2,1–2,8M ciclos/frame (3–5 fps)** frente a 142,1k (49,9 fps) sin dibujo y ~142k con la sala prerenderizada. Diferencia ≈ **15k ciclos por job** (blit + `wait_blitter` por job, contrato de escritura con el Blitter ocupado, `amiga_blitter.cpp` DT-022). La **misma** carga por la racha de streaming (`clear_now`/`copy_now`, un job por operación) se mide **dentro del ruido del presupuesto**.
2. **`draw_text_words` por frame es el trabajo más caro de una escena de juego.** 3 líneas de 40 caracteres en un playfield de 4 planos: ~750k ciclos/frame (≈6 campos). Además, si alterna frames rápidos y lentos, la cadena de espera de VBlank del bucle `run_frames` se **atasca** (frames congelados, capturas byte-idénticas). Con la composición separada de la publicación (tira Chip + un `copy_now` por plano) el régimen vuelve a 48 fps estables y el texto se ve en ambos buffers del doble buffer.
3. **Herramientas (familia DT-023):** `measure-fps` falló intermitentemente leyendo `$b7e928` (timeouts GDB de 21,5 s) y el runner dejó `screenshot.png` **obsoleto** con `missing_screenshot` (las capturas de secuencia sí se escriben). Las lecturas del canal lateral pueden quedar **congeladas** tras una pausa de captura.

**Plan:** (el problema 2 es específico de la demo y ya está resuelto ahí; 1 y 3 son de engine/herramientas)

- [ ] Documentar en `ZERO_COST_FRAME_PATH.md` el **coste por job** medido y la guía de decisión «pocos jobs por frame / streaming para muchos objetos pequeños» (la promesa actual «encolar es barato» sólo vale por job, no por cantidad).
- [x] **Herramientas (2026-10-17):** `measure-fps` reintenta el read de `$b7e928` (5 intentos) y **aborta con mensaje claro** si el GDB sigue atascado en vez de tumbar la medida (su `state` del canal lateral NO sirve para ciclos: ~14x, no está en unidades de CPU_HZ). El runner **borra el PNG antes de capturar** y reintenta por canal lateral: un fallo ya no deja `screenshot.png` obsoleto.
- [x] **Herramientas (2026-10-18, hilo HUD):** `measure-fps` muestrea por **sub-ventanas** (8 por defecto) y reporta min/max/estabilidad: una media única engañaba cuando el frame está en el borde del campo (la 302 alternaba 1 y 2 campos y daba 26, 33 o 49 fps según la fase de la ventana; con la dispersión el aviso salta solo). Si ninguna sub-ventana avanza frames, reintenta y aborta indicando la dirección del run-status y el `detail`. La **instancia propia** del tool sigue sin arrancar sano en este entorno (`$b7e928` timeout de 21,5 s + run-status sin avanzar); el camino `run-demo` + perfilador + `HudFpsWindow` en pantalla sí mide (recomendado en el procedimiento, regla 15). Pendiente: revisar el launch propio del tool (¿`runner.uae`/instancia huérfana?).
- [ ] Investigar el atasco de la cadena de espera de VBlank con cadencias mixtas (repro: 001_rpg_dungeon con HUD compuesto 1 de cada 25 frames; ver `dist` de la demo en `VALIDATION.md`), en la línea de `vblank-timer-inconsistencies.md`.

**Evidencia:** `demos/features/rpg/amiga/001_rpg_dungeon/VALIDATION.md` (tabla de medidas y ablaciones); código y comentarios de coste en `src/main.cpp` de esa demo.

## DT-026 — Demo 116 (`flatshade_convex`): 21,62 fps (< suelo 25) — campaña de paridad CERRADA sin asm, vías agotadas y medidas

**Origen:** modernización de 079/116 (2026-10-17): trazado por lote (`retro::wire_lines`), loop `run_frames`, servicio de Blitter off, HUD por zona de 1 bpp sin coste por frame y soporte `bbox` en `flat_shade_xor`.

**Medidas (`measure-fps`):**

- 079: **6,86 → 35,7 fps** (7,29 → 1,40 campos/frame); tras el fix del HUD a plano uniforme, 33,1 fps (1,51 campos; el 5º plano cuesta ~0,11 campos); con el build **release** (`-O2`) y el fix de contadores de la IRQ (abajo), **37,04 fps** (1,35 campos). Cierra el problema reportado («se pierden frames»).
- 116: **19,20 → 19,70 fps** (2,54 campos/frame); tras el fix del HUD a plano uniforme, 18,05 fps (2,77 campos; el 5º plano cuesta ~0,23 campos); **release = 19,22 fps (2,60 campos)**. **Sigue por debajo del suelo de 25 fps** del efecto de relleno de polígonos → demo **ABIERTA**.

**La referencia original, medida (no supuesta):** la demo original (`demoscene-repo-orig`, ADF en emulador ciclo-exacto, perfil WinUAE + capturas por campo) **NO mete el frame en un campo**: sus frames salen en **parejas de campos idénticos** (la imagen avanza 1 vez cada 2 campos ⇒ **25 fps**, 2,0 campos/frame) — coherente con los números de su propio profiler (`PROFILE`: transform 156 + draw 130 + fill 289 = 575 líneas ≈ 1,84 campos). DMA por campo (contadores del recorder, `include/debug.h:361-372`): CPU 17,8k, Blitter 26,8k, Bitplano 16,4k, refresh 1,2k; CPU ocupada 142102/142112 (saturada). **El objetivo realista de paridad es 2,0 campos (25 fps), no 1.**

**Brecha medida release vs original (por frame completado):** original 2,0 campos × 62,2k accesos DMA/campo ≈ 124k; port 2,6 × 62,6k ≈ 163k (+39k). Desglose del exceso: **CPU +45 %** (51,5k vs 35,5k accesos/frame: codegen `-O2` de g++ vs gcc de Bebbo con pistas `register asm`, más trabajo de plan/servicio por job) y **Bitplano +20k/frame** (5º plano del HUD —~5k— y ventana compartida/offsets). El Blitter total por frame es similar en ambos. El camino de cierre es el mismo del plan: async (solapa la CPU con el Blitter como el original, cuyo clear se absorbe bajo el transform) + ruta asm del transform (1,8× medido).

**Hallazgo colateral (HUD, corregido):** el HUD de la primera pasada usaba una «zona» de 5º plano activada con un WAIT `>255`; con la semántica `>=` del comparador (`custom.cpp:4416` `coppercomp`) dispara en la raster 255 y **la lista termina ahí (STOP)**: el plano quedaba encendido (franjas de basura). Corregido con **plano activo todo el frame + paleta espejo** en ambas demos (detalle en `PROFILING_FROM_AGENT.md` y los `VALIDATION.md`).

**Hallazgo colateral (contadores, corregido):** la IRQ de VBlank pisaba `context.frame.frame_index` con el contador de **latidos** (`engine.hpp`, `VBlankHeartbeat::run`): una demo que lee `frame_index` en `render` (tras >1 campo de trabajo) veía la tasa de campos (~50/s) en vez de la de frames completados — el HUD de fps recién añadido destapó el caso (mostraba 49,2 donde la tasa real era 32,7). Corregido separando `field_index` (latidos, lo fija la IRQ; pacing del fondo) de `frame_index` (frames completados, lo fija el bucle). Test: HOST-466.

**Hallazgo clave (acotado y medido):** el fill XOR acotado a la caja del objeto («bbox», soporte nuevo de `eng::retro::flat_shade_xor(…, bbox)`) y el clear acotado por buffer **empeoran** el frame en la pipeline síncrona actual (19,2 → 17,3 fps): el fill acotado son **4 jobs** (uno por plano) cuyo `wait_blitter` interno serializa la CPU con el Blitter dentro del `update` (el barrido completo era **un solo job** lanzado sin esperar). La vía correcta para cobrar el recorte (~56k de fill + ~40k de clear) es el **referee async** del `FramePlan` (encadenado por IRQ de fin de blit, viable bajo el SO según la 066/`066-async-blit-irq-bajo-el-so.md`).

**Estado del prototipo async (2026-10-17, jornada 2):** el port de la 116 a `execute_frame_plan_async` (líneas EOR + AreaFill acotado + ClearRect por plano, un plan por frame) está **reconstruido, SIN cuelgue, detrás de `-DFLATSHADE_ASYNC=1`** y con **veredicto medido negativo**: 8,9 fps (5,6 campos) frente a 18,05 de la vía síncrona. Tres causas raíz encontradas y corregidas por el camino (todas con fix de engine o de demo):

1. **Aristas horizontales en el plan**: `blitter_line_eor_prepare` devuelve `false` para ellas (se descartan, como en el pintor): el job no arranca ningún blit pero el encoder devolvía éxito → la cadena esperaba una IRQ que nunca llega. Corregido en `build_chain_plan` (skip `y0==y1`).
2. **El feeder async se despachaba desde contexto de tarea**: `wait_blitter()` llama al servicio de blit registrado mientras espera `BBUSY`; con el feeder instalado, la espera `while (m_async_busy) wait_blitter()` de `execute_frame_plan_async` avanzaba la cadena EN PARALELO a la IRQ real (doble avance por job → descuadre/cuelgue). Corregido con la bandera `g_blit_service_referee_only` (`amiga_internal.hpp`, la pone el instalador async): `wait_blitter()` hace sondeo puro cuando el servicio es el feeder.
3. **El giro de espera expoliaba el bus**: un sondeo pelado de `m_async_busy` roba slots al Blitter de la cadena (×2 de duración). `frame_plan_async_wait()` (engine) y el giro de la demo ceden el bus con BLTPRI (`wait_blitter`).

**Coste estructural (medido por ablación):** con la cadena de solo-líneas el frame sube a 4,15 campos para ~51 blits cortos → cada job paga ~3,5-4k ciclos de arranque de Blitter (~1,4k) + IRQ de nivel 3 + re-programación en la ISR. La vía síncrona encadena los mismos blits sin round-trip de IRQ (~1,9k/job). **Conclusión: el referee async es para cadenas CORTAS de blits LARGOS (los runs de cookie-cut de 066/212), no para listas de 50+ líneas** — la infraestructura y los tres fixes quedan; la demo vuelve a la síncrona.

**Experimentos de recorte, medidos en aislado (release, esta jornada):** clear acotado a la bbox (4 jobs de 1 plano, esperando solo el último) = **19,24 fps vs 19,26 del barrido completo → sin cambio** (el clear ya está oculto en la pipeline de 3 buffers: la lectura de septiembre «el clear son 68k expuestos» NO aplica al árbol actual); fill acotado a la bbox = **16,81 fps → peor** (la serialización de los 4 jobs dentro del update se come el recorte: confirma la lección de septiembre en aislado). El umbral de 25 fps NO está en el clear: está en las **líneas (~114k) y el fill completo (~131k)**, que solo bajan con la ruta asm del draw (rota, ver abajo) o una técnica distinta.

**Ruta asm del transform/draw:** el desfase de contorno tenía una causa concreta más: `fs_draw_edges` elegía el octante con la condición **invertida** (`x1 < x0` en vez de `x0 >= x1` para AUL/SUL; contraste `line_prepare_impl`, `amiga_blitter.cpp:854`). **Corregido** en `support/flatshade_asm.s` — pero la ruta sigue **NO VALIDA**: el build `-DK_FLATSHADE_ASM=1` dibuja rayas (más roto que el «desfase de 1-2 px» de septiembre) y no avanza frames; el default (`K_FLATSHADE_ASM=0`) queda intacto (18,12 fps debug / 19,26 release, contorno correcto).

**Plan:**

- [x] `BlitJobKind::AreaFill` en el encoder/ejecutor (A=D, `FILL_XOR|DESC`, rect + stride por planos) + validación/presupuesto del plan — base lista y ya **ejercitada en target** por el plan async de la 116 (flag `FLATSHADE_ASYNC`).
- [x] Diagnóstico del cuelgue del protagonista async: tres causas raíz (horizontales en el plan, feeder despachado desde tarea, giro sin BLTPRI), todas corregidas (`g_blit_service_referee_only`, `frame_plan_async_wait` con nasty, skip de horizontales). El prototipo completó cadenas y se midió: **8,9 fps vs 18,05 síncrona** → veredicto negativo para cadenas largas; la vía queda documentada para cadenas cortas de blits largos.
- [x] Experimentos de recorte en aislado (release): clear-bbox sin cambio (19,24 vs 19,26), fill-bbox peor (16,81). El umbral de 25 fps está en líneas+fill, no en el clear.
- [ ] **Arreglar la ruta asm del draw** (`flatshade_asm.s`): el octante invertido está corregido, pero el build `K_FLATSHADE_ASM=1` sigue dibujando rayas (regresión posterior a septiembre: entonces dibujaba con desfase de 1-2 px). Máximo candidato para cruzar 25 fps si el transform asm (1,8× medido) se puede usar. Diagnóstico siguiente: comparar `fs_transform_vertices` contra `lib3d::transform_vertices` con `freeze-diff` sobre el wireframe (`FLATSHADE_SKIP_FILL=1`).

**Estado jornada 3 (2026-10-17, campaña de paridad por EL API DE ALTO NIVEL — el usuario descarta la vía asm como truco):** release **19,21 → 21,62 fps (2,60 → 2,31 campos)** con: HUD en **banda superior legal** (WAIT 44/58; +1,7 fps — enmienda a DT-027: las zonas ≤255 son la vía canónica), **`wait_blitter` restaura BLTPRI** en vez de apagarlo (el original corre con BLITHOG global; +0,33), **`-O3` en la TU de la demo** (`build.args`; +0,33) y **long-writes de pares contiguos en `blitter_line_draw`** (+0,08). Órdenes de pipeline medidos: clear al final = 21,23; antes del transform = 20,50; al principio del siguiente update = 21,01 → se conserva el orden propio. **Suelo del pipeline actual** (ablaciones release: transform+overhead 142k = 1 campo; fill ~66k, edges ~112k, clear ~100k expuestos): ~262k de blitter serial = 1,85 campos ≈ 27 fps con solape perfecto; la holgura restante (~44k) es espera por línea del contrato de registros del Blitter + cuantización a VBlank. **La 116 sigue ABIERTA** (21,62 < 25); próximos candidatos de engine: batching del camino de líneas, sin trucos de demo.

**Cierre de la campaña (2026-10-17):** final **release 21,62 fps / 2,312 campos** (debug 20,51) frente a los **2,00 campos (25 fps)** de la original medida. La brecha (~0,31 campos ≈ 44k) está acotada y explicada: el blitter serial (262k = 1,85 campos) **cabe** en 2 campos; el exceso es el acoplamiento BLTPRI (el CPU se estira ~40-60k al solapar los blits largos — el transform de 90k bajo el fill de 131k) + espera por línea (hardware: en modo LINE los registros se leen en vivo, no hay shadow-programming) + cuantización a VBlank.

**Todos los intentos de rendimiento realizados, con veredicto medido** (detalle fila a fila en `docs/guides/optimization/OPTIMIZACION_GPP_68000.md`, entradas 2026-10-17):

| Intento | Resultado |
|---|---|
| Referee async del `FramePlan` (líneas+fill+clear encadenados por IRQ) | 8,9 fps vs 18,05 — refutado para cadenas largas de blits cortos (coste por job ~3,5-4k) |
| Órdenes de pipeline: clear primero / pre-transform | 20,43 y 21,02 vs 21,73 (el orden propio gana; medido 4 veces) |
| Clear acotado a bbox (4 jobs de 1 plano) | 19,24 vs 19,26 — sin cambio (el clear ya está oculto) |
| Fill acotado 4-jobs-por-plano | 16,81 — peor (serialización de los 4 jobs) |
| Fill acotado **un blit contiguo** (`blitter_area_fill(..., bbox)`) | ≈0 fps (el ahorro cae en la cola absorbida); **pixel-exacto**, se conserva por menos bus |
| Hoist de `BLTDPT` al preludio | +0,11 y **REVERTIDO: rompía la paridad del contorno** (el Blitter muta D en modo línea) — rayas horizontales |
| `ENGINE_OPT=-O3` | 21,61 vs 21,62 — sin cambio |
| Nasty selectivo (off en la fase de líneas) | 21,63 vs 21,72 — wash (el wait por línea rearma nasty) |
| Interleaving cooperativo del transform en las esperas (`FLATSHADE_PUMP`, bomba global y con ventana) | 14,7 / 14,8 vs 21,62 — refutado (el bloque síncrono ya corre bajo el fill; la bomba ejecuta bajo nasty, desborda esperas cortas y drena en serie) |
| Ruta asm del transform/draw (`flatshade_asm.s`, 1,8× medido) | rutas no válida (rayas); descartada por directiva (nada de asm/trucos de demo) |
| Knobs de IRA/scheduling en la TU de la demo (`-fira-region=all`, `-fno-ira-share-save-slots`, `-fira-algorithm=priority`, `-fcaller-saves`) | 21,61 / 21,66 / 21,49 / 21,62 vs 21,62 — wash (sin presión `ADDR_REGS` en la disposición C++) |
| Cirugía de codegen en `eng::math::arith<s16>` (quitar el `ext.l` por operando del idioma `muls`; forma con operandos `s16` directos) | **21,62 → 21,67..21,72** (2,312 → 2,302..2,307; `ext.l` 12→1 en el update, mul32=14 sin libcalls nuevos; 101 sin regresión) — conservada |
| Espera explícita del fill (`area_fill_wait=true`, como el original) | 17,24 / 2,90 campos vs 21,67 / 2,31 — refutado (el fill de 131k expuesto en el update no se recupera; la cola colgada tras el draw y absorbida en la espera del siguiente update gana) |

**Paridad de codegen medida (2026-10-17, cierre de jornada 3):** con el fix de `arith`, el bloque transform+visibilidad baja de 89,6k a **72,4k** (mat 5,9k, caras 19,1k, aristas 11,8k, vértices 35,8k) ≈ **la referencia (71k)**, y el draw queda en 64k vs 59k. El hueco restante (0,30 campos ≈ 43k) es **estructural**: cola de fill+clear (196k de blitter) desbordando la ventana de vblank (54k de espera medidos) + cuantización 2/3 campos (sueño ≈127k). La cirugía #2 pendiente (matriz lineal, cache fuera de pila) apuntaría a ~1,5k de trabajo YA OCULTO → **descartada por predicción ≈0**.

**Oráculo de codegen del binario original (2026-10-17, cierre de jornada):** extraída la imagen HUNK del `flatshade-convex.exe` con `tools/analyze/hunk-image.mjs` (base 0, relocs aplicadas) y desensamblada por funciones (direcciones del `.map`). Su `TransformVertices` es **~30 instrucciones/vértice con CERO pila** (matriz con puntero en registro y `(a0)+`, x/y/z residentes, `divs.w`, pins `register asm` respetados por el gcc de Bebbo); el nuestro lee la matriz por dirección absoluta con `ext.l` y derrama el cache del proyector a pila — la grasa del 1,26× (89,6k vs 71k). El efecto original entero son 1528 bytes. **Candidatos siguientes (cirugía de codegen en el engine, sin asm): matriz lineal `s16[12]` caminada por puntero en el proyector y cache fuera de pila en `transform_vertex_one`.** La vía de toolchain (AmigaPorts/Bebbo g++) queda como decisión de usuario.

**Única vía restante identificada** (decisión de producto del usuario): rediseño de la técnica del contorno (menos operaciones de blitter — p. ej. contorno por Copper o segmentos largos) o asm *de engine* testeado para `lib3d` (la original NO usa asm: gana por codegen del compilador). Nada pendiente en el camino del API de alto nivel.

**Decisión de usuario (2026-10-17, cierre final):** se **deja la 116 en 21,67..21,72 fps** (2,30 campos). Con el codegen a paridad medida y todas las vías de pipeline/orden/solape medidas y agotadas, lo único que queda para cruzar 25 fps es **reducir trabajo de blitter** (contorno/fill/clear) = cambio de técnica de dibujo (p. ej. contorno por Copper o por segmentos largos), una decisión de producto aparte; las vías de asm de engine y cambio de toolchain quedan con **ganancia esperada baja** ahora (el codegen ya está a paridad: 72,4k vs 71k de la referencia). El prototipo async, la bomba de interleaving y la cirugía de codegen quedan documentados y con flags por si se retoman.

**Evidencia:** `demos/techniques/amiga/effects/116_flatshade_convex/VALIDATION.md`; notas de coste en `src/main.cpp` de 079/116; perfiles en `out/debug/*` de la campaña.

## DT-027 — HUD ligero obligatorio al medir rendimiento (recomendación para hilos futuros)

**Origen:** campaña 079/116 (2026-10-17) y hallazgo previo de la RPG 001 (DT-025).

**Lección medida:** un HUD de texto dibujado **por frame** contamina la medida de fps: 3 líneas de 40 caracteres en 4 planos ≈ **750k ciclos/frame** (DT-025) y un prototipo de franja pintada cada frame en los 5 planos de la 079 ≈ **205k ciclos/frame** (medido por ablación A/B: 17,45 vs 36,27 fps). El HUD de **zona aparte con 1 bpp** y actualización **solo al cambio** mide **0 ciclos/frame** (las 079/116 modernizadas lo usan y mantienen su fps íntegra).

**Regla para futuros hilos** (amplía §0 de `AGENTS.md`, que exige el HUD más sencillo posible):

- Al **medir rendimiento** (y en general), el HUD va en **zona aparte** de la pantalla con **1–2 bitplanes propios** activos **todo el frame** (`BPLCON0` + puntero en la cabecera de la lista), **nunca** repintado sobre los planos del efecto ni por frame.
- **Prohibido cerrar el HUD con una «zona» de WAIT `>255`**: el comparador del Copper usa semántica `>=` sobre `V[7:0]` (`custom.cpp:4416` `coppercomp`), un WAIT por encima de la 255 dispara en la raster 255 y **no se puede volver a esperar nada después** dentro de la lista: el plano extra queda encendido más allá de su banda (franjas de basura; pasó en 079/116, 2026-10-17). **Las bandas con WAITs ≤255 SÍ son la vía canónica y la más barata** (enmienda 2026-10-17, jornada 3): la 116 enciende su 5º plano en la raster 44 y lo apaga en la 58 (banda superior) y el fetch del HUD baja de ~290 a ~14 filas por campo (**+1,7 fps medidos** vs el plano activo todo el frame). La colocación del texto es la misma con puntero fijado en la raster de encendido (el fetch va ~10 filas por delante de la visualización).
- El **delta** del plano fijo se absorbe con la **paleta espejo** (`palette[16]=tinta`, `palette[16+k]=palette[k]`) para que sumar +16 al índice de color sea transparente fuera del texto; el coste queda en el fetch fijo (~0,1–0,25 campos según demo), medido y anotado.
- Etiqueta estática UNA vez; campos dinámicos en **campos de ancho fijo** reescritos ~1 vez/segundo (~56 escrituras); texto **por palabras** (`Surface::draw_text_words`).
- **El HUD muestra el fps del último segundo** con un decimal, junto al contador de frames totales (079/116, 2026-10; `eng::debug::HudFpsWindow`, `prof.hpp`): la ventana se cierra al acumular ≥1 s del reloj de ciclos (0xB7E928) y da la **media de la última ventana** — un cambio de cadencia se ve al segundo. El coste por frame es una comparación (~10-15 ciclos); la división se paga solo al cerrar la ventana. Usar un **contador de frames fiable** para la ventana: `frame_index` cuenta frames completados (el bucle), **no** latidos de VBlank — la IRQ publica esos en `field_index` (bug real corregido 2026-10, caso HUD a 49,2 fps donde la tasa era 32,7; HOST-466). Test del helper: HOST-465.
- Antes de dar una medida de fps por buena, revisar el coste del HUD (ablación o profile) y dejarlo anotado en el `VALIDATION.md`; el campo de fps del propio HUD debe **coincidir con `measure-fps`** en régimen estable (validación cruzada barata: dos capturas separadas y su delta de contador).
- Detalle operativo y casos medidos: `docs/tools/PROFILING_FROM_AGENT.md` §«El HUD no puede contaminar la medida».


## DT-028 - Escrituras fuera de la reserva en la emisión de blits/copper (RESUELTO, con pendientes)

**Origen:** auditoría del port de `circles` (demo 122) y síntoma reportado de «buffers que se contagian»: una escritura indebida no venía del asignador (`BlockPool`/`MemBank`), sino de caminos de emisión que calculaban una dirección fuera de la reserva del destino.

**Causas localizadas (con evidencia):**

- `graphics::bob_draw`/`bob_erase_box`/`bob_save_box`/`bob_restore_box` no comprobaban el borde derecho/inferior del `BobTarget` ni que `planes.size()` cubriera la geometría (`row_bytes · height · planos`, o `(planos-1)·plane_step + row_bytes·height` con paso explícito). Un BOB en el borde o un target con reserva corta escribían en el buffer contiguo.
- `FramePlan::begin_blit_job`/`begin_line_job` devolvían `m_blit_jobs[m_blit_job_count]` sin comprobar la capacidad: con el plan lleno, el llamador escribía en `m_blit_jobs[max]` (pisando el siguiente miembro del plan). El `commit` fallaba después, pero el daño ya estaba hecho.
- `copper::ListBuilder::write_pair`/`move32` calculaban `capacity - 2` con `u16`: con capacidad 0/1 envolvía y escribía a través de un buffer vacío.
- `Screen::stamp::at`/`clear_now` (racha de blits en streaming) no acotaban contra la geometría del destino; `clear_now` ignoraba `box.x` y podía borrar palabras de más.

**Arreglo:**

- `BitmapView::storage_bytes_used()` + `bob_detail::target_valid`/`box_fits`; bounds en los cuatro caminos de BOB y en `scene::clear_box`.
- Ranura de **descarte** (`FramePlan::m_scratch`) para `begin_*` con el plan lleno; `commit` falla y no se consume ranura.
- Chequeo `capacity < 2` en `ListBuilder`.
- `Screen::stamp::at`/`clear_now`: límites de destino, dirección de `clear_now` con `box.x` alineado a 16 y `dmod` correcto.

**Gates:** HOST-232 (círculo/clip), HOST-388 (plan lleno), HOST-439 (fuera de buffer/reserva corta/límite exacto/shift, canarios) y demo 122 (`build -> run -> analyze`, 49,92 fps, visión limpia). La 213 (streaming con 16 BOBs) se midió tras el cambio: **49,97 fps / 1,001 campos** (el chequeo de `StampRun::at` se compensó usando `mulu.w` en vez de `__mulsi3` para el offset de fila/frame).

**Hecho después de este registro:** contrato de tamaño de la hoja del BOB (`frame_bytes`/`mask_bytes`) con los 9 tests de hojas débiles endurecidos; validación genérica de `BlitJob` en el `FramePlan` (`TargetBounds` + `blit_job_within`); barrido de `Block::view.data()` con el gate `demo-raw-writes`; y canarios de audio en target (`tests/amiga/l1_backend/002_audio_leases`, L1-002). Pendiente (no bloquea): canarios de capacidad en `SpriteManager`/`copper::Plan`/`TileScrollDriver`. Plan en `ROADMAP_API_COHERENCE.md` §F8 y `ROADMAP_MEMORY_OWNERSHIP.md` Fase 8.

## DT-029 - Demo 011 (`typed_memory`): no alcanza READY (state=0, `PC=0x4`) - RESUELTO

**Síntoma:** `bash ./tools/run/run-demo.sh demos/techniques/amiga/setup/011_typed_memory` agotaba los 40 s con `state=0`/`detail=0x0`. En vivo (canal lateral) el CPU queda en `PC=0x4`, `SR=0x14`, `A6=0`, antes de que `reset()` del run-status llegue a ejecutarse.

**Causa:** la demo pide `FAST_STACK=1` (`build.args`) y su `_start` llama a `eng_fast_stack_alloc` **antes** de los constructores y de `main`; esa función usaba `AllocMem` con `SysBase` aún `NULL`, de modo que saltaba a `(a6=0) - 0xc6` y moría. No era un fallo del bootstrap: con el `entry.hpp` de HEAD pasaba igual.

**Arreglo:** `eng_fast_stack_alloc` inicializa `SysBase` desde el vector 4 si es `NULL` antes de `AllocMem` (`support/gcc8_c_support.c`), que es la capa de plataforma correcta para el arranque previo a `main`.

**Evidencia:** `run-demo` alcanza READY y captura screenshot con el binario nuevo; desensamblado verificado en MCP (`eng_fast_stack_alloc` carga `SysBase` y solo entonces ejecuta el LVO de `AllocMem`).

## DT-030 - Barrido de flota tras los bounds: 214 fuera de presupuesto; 215 resuelta - ABIERTA

**Origen:** paso 5 de cierre de F8 (comprobar que los bounds/contratos no degradan las demos de `stamp`/plan).

**Medidas (`A500_debug`, `measure-fps`, 20 s) y A/B:**

- Sin regresión en las demos tocadas por los bounds: **122** 49,87/1,003 (release 49,92/1,002), **213** (streaming `stamp`/`clear_now`) **49,92/1,002**, **117** 16,67 (fleet: 16,61).
- **215_app_bobs: RESUELTA — 49,92 fps / 1,002 campos** (antes 24,94/2,005). Limpieza **acotada** por huella del actor (`clear_box` encolado, una caja por actor) + display de **2 planos** (la paleta usa 0..3) + limpieza entera una sola vez; el DMA de Blitter baja de 10,6k/31,0k a ~0,9k accesos/frame. Visión canónica sin artefactos. Detalle en `demos/.../215_app_bobs/VALIDATION.md`.
- **214_app_sprite: 30,96 fps / 1,61 campos** (antes 0,70 / 71,5; mejora 44×), **abierta solo por cadencia**. **Causa raíz medida**: re-materializar el fondo indexado por CPU es infactible — cada acceso a Chip cuesta ~30–40 ciclos con el DMA de bitplanes activo (ablación: 20.480 words escritos en bucle = 7,73 fps; escanear los runs sin pintar = 1,40 fps / 5,05 M; release igual). El escritor CPU (`fill_run`/`draw_run_words`) no cambia la medida. **Corrección aplicada**: pre-render planar una vez (`c2p_1x1_naive`) + copia de la ventana por Blitter con `screen.bitmap(asset, destino, palabra_origen, shift)` (un job de 4 planos), scroll 1 px suave. La copia completa cuesta ~0,5 campo, así que el frame reparte 1–2 campos. **Hallazgos**: `Screen::bitmap` no pintaba por un bug latente (módulos fijos a 0 en la rama contigua; **arreglado** con módulos desde los strides + ventana con origen/shift + test HOST-232) y la escena de **2 planos** es patológica (base sin copia 853 k vs 154 k con 4 planos). **Siguiente paso:** capa de scroll por hardware para 1 campo estable — requiere que los actores vivan en un playfield aparte (PF2/dual playfield), porque el display leería el asset directamente y un BOB dibujado en él se movería con el scroll. Detalle en `demos/.../214_app_sprite/VALIDATION.md`.

**Hallazgos colaterales (2026-10, sin resolver):**

- **Escena de 2 planos: el bucle se queda bloqueado en `Wait()` de VBlank a los ~41 frames** (determinista; CPU en el `STOP` del task idle del SO y contador de frames congelado). **No** es un trap: un stub en los vectores de bus/address/illegal/div0 no se dispara y el estado queda Ready. Con **4 planos** el mismo binario avanza con normalidad (511→945 frames en 5 s). Estado de hardware en la congelación (canal lateral + memoria): `DMACONR=$03C0` (DMAEN/BPLEN/COPEN/BLTEN), `INTENAR=$8022` (**VERTB habilitado**) y `INTREQR=$0011` (VERTB no pendiente fuera de VBlank). Repro: 214 con `color_depth = 2`. **Causa confirmada**: el SO **re-toma el vector 0x6C** (en el cuelgue apunta al dispatcher de la ROM; en el mismo binario con 4 planos apunta al handler del engine en Chip). El engine solo re-arma INTENA (que conserva VERTB) y no reinstala el vector, así que la señal de VBlank nunca llega y el `Wait()` queda muerto. **Intento de arreglo revertido**: registrar el handler como **servidor Exec** (`AddIntServer`, un nodo por fuente VERTB/BLIT con stub ASM); con el servidor el cuelgue pasó al frame 23 con `INTREQ=$1040` (BLIT pendiente sin despachar). Detalle y receta siguiente en `docs/debugging/investigaciones/212-vblank-irq-apagada-por-el-so.md`.
- **`screen.text` en escena de 6 planos**: el cuelgue observado **no reproducía** (artefacto transitorio de builds con WIP ajeno a medias); con el engine actual la 060 migrada alcanza READY, dibuja el self-check y mide **49,92 fps / 1,00 campos**. La migración queda hecha (baseline de ergonomía 133→132).

**Siguiente paso:** capa de scroll por hardware con los actores en PF2 (1 campo estable) e investigar la escena de 2 planos.

## DT-031 - Gate `raw-pointer-members` en rojo por dos observadores crudos ajenos al HUD - CERRADA

**Síntoma:** `node tools/check/raw-pointer-members.mjs` falla en `master` con dos claves fuera de baseline: `eng/core/math/fixed_affine.hpp:a` (miembro `const affine_t* a` del `projector<Affine>::cache`) y `eng/retro/wireframe.hpp:clip` (parámetro `const Rect* clip = nullptr` de la emisión de líneas). Ninguna de las dos cabeceras está modificada por este hilo.

**Evidencia:** salida del gate (`2 puntero(s) a objeto no propietario sin justificar`); `raw-pointer-members-baseline.txt` solo lista `fixed_affine.hpp:m0/m1/m2`, no las claves nuevas. Las cabeceras proceden de commits recientes de los frentes 116/wireframe (`81ef2e27`, `e533c1fe`).

**Impacto:** el gate corre en la pasada completa de `tools/run-host-tests.sh`; mientras esté rojo, la suite completa no puede ponerse en verde y el trabajo nuevo no puede distinguir «rojo propio» de «rojo heredado» (§1.5 y §1.7).

**Plan:** (1) `cache::a` → `eng::Ref<const affine_t>` (el acceso `c.a->` no cambia; `make` lo construye desde `&a`); (2) `clip` → `eng::Ref<const eng::retro::Rect>` con `clip.valid()`; (3) re-ejecutar el gate y los HOST de wireframe/116 y retirar de la baseline cualquier entrada asociada. Coordinar con el frente 116/wireframe activo antes de tocar ambas cabeceras.

**Resolución (2026-10-17, frente 116/wireframe):** aplicado el plan en ambas cabeceras — `cache::a` es `eng::Ref<const affine_t>` (anulable, para el estado persistente del interleaving; `make` construye desde la referencia) y `wire_lines(..., eng::Ref<const eng::retro::Rect> clip = nullptr)` con `clip.valid()`; los llamadores con `&rect`/`nullptr` no cambian (conversión implícita). Verificado: **gate OK**; HOST-213/047/050/053/011/013 verdes; builds release de 079/116 OK; 116 medido **21,64 fps / 2,311 campos** (banda 21,62..21,72, cambio de tipo sin coste) y `asm-audit` TOTAL `mul32=14` sin cambios (codegen idéntico). La baseline conserva solo las entradas `fixed_affine.hpp:m0/m1/m2` (locales del camino caliente, deuda aceptada aparte).

**Estado: CERRADA** (resuelta por el frente 116/wireframe, que es el autor de ambas cabeceras).

## DT-032 - `PatchHandle` corrompe la copperlist activa en escenas sin `commit()` por frame - ABIERTA (mitigada en 080)

**Síntoma:** en la demo 080 (`3d/080_camera_pan_window`) el frame quedaba efectivamente colgado (~0,2 fps; el contador de frames avanzaba 1 por ventana de 5 s) y la palabra 0 de la copperlist activa aparecía corrupta (`word0=0x58B0` en vez del opcode del `MOVE DMACON`).

**Evidencia:** `profile.mjs` mostró 89 frames/4 s con `-DK080_SKIP_PATCH` (parcheo desactivado) frente a 1 frame/6 s con el parche; aislado con `-DK080_PATCH_NO_BPLCON1` vs `-DK080_PATCH_NO_PLANES`: el parche de `BPLCON1` hecho con `PatchHandle::set` (`Scheduler::patchable`) ralentiza el bucle ~100× y corrompe la lista; sustituido por `move_at` + `copper::patch_data(active_span())` la demo vuelve a ~22 fps. La 080 no llama a `Scene::commit()` por frame (gestiona los buffers a mano) y el handle escribe a través del emisor del `Plan`.

**Ampliación (2026-10-18, perfil por secciones + ablaciones por intervalo):** el parche de datos de los punteros `BPLxPT` sigue sin ser seguro sobre la lista **activa** durante el display, aunque use `active_span()`: con el clip corregido (DT-041) la demo avanza pero `g_eng_prof` (leído por canal lateral) acusa **53-58 M ciclos por llamada en `kProfPatch`** (13/13 llamadas; `Frame`/`Full`/`Fill`/`Clip` sanos en el mismo run) y el proceso termina en un bucle de ROM del Kickstart (`PC=0x00FC0F90`, SR supervisor) tras ~13 frames. Ablaciones con la misma ventana (8×400 ms): build completo → frame 12 y parado; `-DK080_SKIP_PATCH` → corre a ~24 fps hasta frame 36; `-DK080_PATCH_NO_BPLCON1` → se clava en 68; `-DK080_PATCH_NO_PLANES` → corre continuo (>80). El desencadenante son los writes a la lista activa; el candidato de corrección es publicar la lista ya parcheada (buffer inactivo + `commit` por frame, piezas en `present_controller.hpp`) o parchear solo en vblank. Consecuencia de método: las secuencias de contrato de la 080 solo son válidas en ventanas cortas (~12 frames) hasta cerrar esto.

**Impacto:** cualquier consumidor que use `patchable()` sin el ciclo `begin_frame`/`end_frame` + `commit` puede corromper la lista y colgar el frame; es una trampa silenciosa de la API del Copper.

**Plan:** documentar el contrato de `patchable` (requiere el ciclo del `Plan`) y/o validar el slot contra el bloque activo en `PatchHandle::apply`; añadir el caso a un HOST de copper.

## DT-033 - Tooling de medida/captura de la 080 (measure-fps dFrame=0, instancias zombi) - ABIERTA (herramienta; captura por paso revalidada)

**Síntoma:** dos fallos del tooling alrededor de la demo: (a) `node tools/debug/measure-fps.mjs 080_camera_pan_window A500_debug` aborta con «la ventana no tiene frames de juego (dFrame=0)»; `profile.mjs` sobre el mismo binario cuenta frames y ciclos correctamente (89/4 s). (b) instancias `winuae-gdb` huérfanas de corridas anteriores hacen que `profile.mjs`/`measure-fps.mjs` lean una instancia zombi: el perfil reporta «1 frames» o `dFrame=0` con la demo funcionando en la instancia nueva (receta fiable: cerrar las propias instancias huérfanas y lanzar `run-demo.sh` **justo antes** de perfilar). La captura por paso, antes bloqueada con «GDB socket is closed», se completó de nuevo en la verificación de 2026-10-20 (128 frames consecutivos); la captura por intervalo también funciona, pero solo sirve de vistazo y no del contrato.

**Evidencia:** repetido en varias corridas; la resolución de `g_eng_run_status` da runtime `0xc2213a` (map + qOffsets) y el canal lateral responde, pero el campo `frame` no avanza en la ventana del medidor (el `ProfBlock`, que sí avanza, se lee por el mismo canal). La demo avanza con el canal lateral (una captura por intervalo de 1 s da 8 frames distintos con la cámara desplazándose).

**Reintento (2026-10-18, con la 080 ya publicando al inicio de campo):** la captura por paso **funciona** cuando no hay instancias zombis ocupando los puertos (el "GDB socket is closed" tambien aparecia al chocar con una instancia ajena; con `--reset-emulator` completa 4 y 8 frames). Secuencia obtenida con el verificador adaptativo: **invariantes OK y 4/4 pares de contrato** (fake == Full publicado + delta de offsets; costura 1088-1354 px ≈ 2 %, dentro de la tolerancia historica). Nota de fase: el probe pausa DENTRO del update (antes del `patch_window` de ese frame), asi que la captura puede reflejar el estado de patch del frame ANTERIOR; el chequeo de "Fulls consecutivos" acusa ese desfase con firma de fase (dominante medido = 2 con Delta pan 3, con el valor "de costura" en k=2). **Resuelto el chequeo de fase (mismo dia):** el desfase efectivo entre imagen y telemetria varia (0-1 updates segun la cadencia del render), asi que el chequeo de "Fulls consecutivos" se hizo **robusto a la fase**: escanea el desplazamiento dominante k=1..6 entre las dos imagenes y exige un minimo claro con costura acotada (el k esperado queda como INFO). Con eso la secuencia de contrato de la 080 pasa **completa** (`OK: 080 paneo (contenido/HUD/contrato de pixel)`: invariantes + todos los pares + fulls). Queda como nit del runner: cerrar siempre sus instancias (el "socket closed" tambien aparecia al chocar con instancias vivas; `--reset-emulator` lo resuelve).

**Reverificación (2026-10-20):** sin `--reset-emulator`, `bash tools/run/run-demo.sh demos/techniques/amiga/3d/080_camera_pan_window --wait-ms 3000 --settle-ms 1000 --sequence-step-frames 128` capturó f0026–f0153; `verify-080-pan-window.mjs --sequence-dir …` pasó 128/128 frames, 95 pares Full/Fake y 31/31 Fulls consecutivos. El subproblema de captura por paso queda **RESUELTO en este run**; la medida `dFrame=0` y la detección de instancias zombis siguen abiertas.

**Analisis de codigo (2026-10-18, DT-033c):** la ruta por paso (`captureFrameSequenceStep`, `run-demo.ts:671`) PARA la CPU en el probe por frame y captura con `captureFramePreferred` (via preferente = screenshot por canal lateral; reserva = screenshot del monitor GDB con pausa+continue). Con la CPU parada en el breakpoint, la accion `screenshot` del canal (que corre en el hilo de emulacion) puede no servir el frame -> cae a la reserva GDB, y **el screenshot del monitor es el que muere con el socket** bajo la 080 (demo pesada). La ruta por intervalo (CPU corriendo; `captureFrameSequence`) no sufre esto: sus capturas van siempre por el canal. Plan concreto para DT-033c: (i) instrumentar que rama se usa en cada captura del paso; (ii) probar captura por canal con la CPU **corriendo** ~40 ms tras el stop del probe (perdiendo el alineado estricto o re-sincronizando por telemetria de frame); (iii) si el monitor muere por timeout, subir el timeout y reintentar una vez antes de abortar (hoy aborta la corrida entera).

**Plan:** (a) revisar la resolución/estructura del run-status del medidor para este binario (comparar con `side_read runstatus <addr>`) y, si es un problema del canal lateral, registrarlo junto a DT-025; (b) que el runner no deje instancias huérfanas y que las tools detecten y rechacen instancias que no responden (o avisen). La captura por paso queda en observación: ya se validó una secuencia larga sin el timeout histórico, pero no se borra el diagnóstico anterior hasta repetir el resultado en una segunda sesión limpia.

## DT-034 - 080: la pasada completa no cabe en los suelos de F1 (base 12,5 Hz / display 25 Hz) - ABIERTA

**Síntoma:** con la técnica ya correcta (contrato de píxel exacto) y la política adaptativa en su sitio (`render_pacing.hpp`, HOST-471), el **display cumple su suelo** (medido ~28 fps, 1,79-1,80 campos/frame con K=2) pero la **base no**: `full` = 426k-551k ciclos (≈3,0-3,9 campos, media ~3,4; el relleno por plano son 106k-310k) → `affordable_fakes` da K=2 y la base queda en ~8-9 Hz, por debajo de los 12,5 Hz. Cerrar F1 exige `full` ≤ 3,0 campos (K=1 con ciclo de 4 campos = 12,5 Hz de base / 25 Hz de display). Nota: el objetivo original de 50 Hz de display quedó **sustituido** por base ≥12,5 Hz + display ≥25 Hz.

**Evidencia:** `demos/techniques/amiga/3d/080_camera_pan_window/VALIDATION.md` §2 (contrato en secuencia) y §5 (desglose medido del Full: `wait` ≈ 300 c, **CPU de la pasada 54k-77k (11-15 %)**, dos `fill_polygons_by_plane` de 117k-337k por llamada — el Full lo domina el relleno, no el CPU; control `SKIP_FILL` = 38 fps / 1,32 campos/frame).

**Medida vigente (2026-10-19, build sólido + culling + `run_frames_polling`):** HUD a los ~3 s de una corrida a intervalos: `FRAME 64 / FULL 24 / FAKE 41 / K 2 / PAN -19`; perfil de 5 s: 124 frames, **287.642 ciclos/frame = 2,03 campos/update** (~24,6 updates/s). K=2 ⇒ `full_fields = 4` ⇒ **Full ≈ 4 campos (~567k)**, base ≈ **8,2 Hz** (coherente con los ~562k previos). El reparto 24 Full / 41 Fake confirma el ciclo Full+2 Fakes. Nota de herramienta: `profile.mjs` necesita `--names Frame,Full,Fill,Patch,Hud,HudCtor,HudLine1,HudLine2,Wait,Clip` (los nombres por defecto son de la 086) y un `.exe` no-ablación (un build con `K080_SKIP_FILL` da `Fill = 0`).

**Rebanado F2 integrado (2026-10-19) y estado de la base:** el Full se ejecuta por etapas (clip → sala plano 0 → sala plano 1 → paneles), una por update, con publicación diferida (`VALIDATION.md` §11). Medido: **1,41 campos/update** (≈35 updates/s), display 32–36 Hz, y publicación cada ~4 updates → **base ≈ 9 Hz**: el rebanado **no reduce el coste total** del Full, solo lo reparte y mantiene la pantalla viva. El relleno por plano mide 114–180k por llamada (una llamada > 1 campo). Para la base quedan las dos vías ya previstas: **(a) solapar** el Blitter con CPU (cola IRQ de F2) y **(b) abaratar el relleno**. La equivalencia de píxel del rebanado está certificada contra HEAD a pan 0/−13/+13.

**Plan (siguiente paso del `ROADMAP_3D_PRESENTACION.md`, F1):** campaña de optimización del Full con el método del repo (presupuesto de bus y mapa de contención, A/B con hot-patch). El desglose medido acota el objetivo: **el relleno por plano domina el Full** (dos llamadas de ~104k-316k en el build arreglado) mientras la CPU de la pasada (transform/proyección/recorte/submit) son 69k-103k, así que microoptimizar el recorte no mueve la aguja: las palancas son (a) el diseño del relleno (dedupe de aristas pares, patrón sin coste extra si el por-plano lo permite) y (b) **solapar el tiempo de Blitter del relleno con CPU/Copper** (la cola/rebanado de F2), que hoy se paga en serie dentro de la llamada. Ya aplicados en el build actual: fast-path de recorte (dentro/fuera sin Sutherland-Hodgman), caja de sala 8..200, caja de panel 52/104, HUD cada 32 frames. (El par de flags vigente es `-O3 -fno-tree-vectorize`, certificado por captura congelada a pan 0; con `-O2` el código actual renderiza partido — la sensibilidad es del codegen y depende de la versión del código: ver DT-038. La CPU de la pasada quedó en 69k-103k medidos en VALIDATION §5.) Análisis de opciones multi-plano (barrido único de planos contiguos, clear D-only, fill con C=D, dedupe de aristas pares) y patrón de relleno (blit `D = A & B` por plano, +82k/+44k medidos en `full`): `docs/reference/amiga/techniques/blitter-line-subpixel-fill.md` §5; el patrón queda acotado por `-DK080_NO_PATTERN` para el A/B.

## DT-035 - Actor con `SaveUnder`: "desapariciones" en las capturas por VIDEO CONGELADO (no es bug del engine) - RESUELTA (artefacto de tooling)

**Sintoma:** con el contenido enriquecido de la 302 (lissajous con `y` variando), la secuencia por paso mostraba el sprite AUSENTE (0 px de sus colores) durante tramos de 3-4 frames, con la pantalla aparentemente congelada (0-4 px de cambio por frame), y el perfilador daba el canal blitter a 0 en esos tramos. Con `y` fija el sprite aparecia 20/20.

**Diagnostico (la captura mentia, no el motor):**
- Contadores acumulados del plan (instrumentacion temporal de `present_sync` via el periferico de depuracion): en ~250 frames, **0 frames con plan no-ok, 0 ejecuciones fallidas**, 1 frame con jobs != 3 (el inicial). El plan se ejecuta SIEMPRE.
- **Self-check del framebuffer**: la propia demo lee sus bitplanes (interleaved, 4 planos) en la caja del actor y cuenta sus colores: **124 px en el frame "desaparecido"** (los pixeles ESTAN en memoria). El bitmap es correcto; la captura por paso servia frames viejos o a medio refrescar (familia DT-023(d): el refresco de video se para en este entorno headless mientras la emulacion avanza).
- El repro host (`out/tmp/dt035/repro2.cpp`: secuencia real + `prev` encadenada + `finalize` + bounds) tambien pasaba limpio: coherente con que el emit/validacion son correctos.

**Leccion / acciones:** (a) para verificar render NO basta el PNG de la captura por paso en este entorno: usar tambien evidencia de MEMORIA (self-check del framebuffer via contadores, como el que se hizo) o la ventana visible; (b) los tramos "congelados" de las capturas no son del demo (anotado junto a DT-023(d)); (c) los contadores acumulados del plan (plan no-ok / ejecucion fallida) son una telemetria barata que conviene dejar disponible.

**Fix de captura (2026-10-18, demo 306):** el modo **`--sequence-step-frames`** (breakpoint en el probe + CPU parada por frame) sirve **video congelado** en este entorno (se confirmo con la 306: 48 frames consecutivos identicos pese a que los contadores probaban que el guion avanzaba). El modo **`--sequence-frames` por intervalos** corre con la **CPU viva** y el video se refresca: la secuencia de la 306 muestra cada par de frames distinto (480-4700 px), 5 firmas de estado y el foco presente en todos. **Regla:** los gates visuales de demos animadas usan captura por intervalos; la captura por paso queda para depuracion puntual, y su verificador no debe exigir animacion.

## DT-036 - Demo 303 (H2): cadencia ~44 fps reales con la CPU muy holgada - ABIERTA (mecanismo acotado)

**Sintoma:** `fps10` (contador del periferico) = **419-424 (41,9-42,4 fps)** frente a **499 (49,9)** de la 302 en el mismo entorno; el frame pierde VBlanks.

**Evidencia consolidada (2026-10-18, instrumentacion temporal en motor y demo, contadores del periferico):**
- **Medida honesta con reloj de VBlank** (`os::frame_count`, 50 Hz exactos; contador `fps10_vb`): **444 (44,4 fps)** — la perdida es REAL (~11% de latidos) pero el reloj del periferico la exagera (incluye tiempo con la CPU fuera del hilo: es un reloj de pared, no de instrucciones). El balance exacto en 15 s: **659 frames / 747 VBlanks** (l_done vs vb_seq, ambos 1:1 con el IRQ) = 88 latidos absorbidos en 47 lotes multi-latido (frames de 2-5 campos; 25 de ellos de 3+).
- **NO es el coste del demo**: secciones medidas con picos — update <=9k, draw_world <=3,2k, present <=2,2k, consume <=1,3k ciclos. El bloque deltas+apply muestra tiempos de PARED enormes (hasta ~700k) pero sus operaciones son baratas por inspeccion (rellenos CPU de <=16x16; **`hw_calls=0`: nunca va por Blitter**) -> **el tiempo de pared de las secciones bajo AmigaDOS vivo no es coste de CPU: esta contaminado por hilos/preempcion** (hallazgo de metodo: los cronometros por seccion con `prof_clock` solo son validos sin otros hilos del SO despertando; los maximos no son atribuibles al codigo).
- **NO es (principalmente) el camino de espera de VBlank**: en modo **polling** (`run_frames_polling`) la cadencia medida es igual o PEOR (389-462 con el mismo reloj; medido en ventana larga de 15 s). El A/B fino por `App::run` no es viable hoy: el parche directo dispara una **fragilidad de codegen del toolchain** (ver DT-040).
- El IRQ de VBlank esta sano: `isr_avg` ~4,9k ciclos, `vb_seq == l_beats` (1:1 con el latido del motor), sin IRQs perdidos.
- La CPU esta holgada (trabajo << campo): la perdida viene de que el bucle principal **no esta listo para consumir el latido** en ~7% de los campos (queda "fuera" 2-5 campos), no de que el trabajo no quepa.

**Plan (siguiente paso):** (a) el perfilador **ya funciona** (receta: ID de demo —no ruta—, `WINUAE_USE_LEGACY_LAUNCH=1`, WinUAE de la extensión, puertos propios; el de `WinUAE-DBG/` no lee `WINUAE_GDB_PORT`); captura ciclos/DMA correctos. **(b) RESUELTO (2026-10-18): el `profileArray` NO es una lista de PCs: es un stream RLE** — una muestra es el PRIMER valor desde el delimitador anterior (el PC), un delimitador `0xFFFFFFFF - ciclos` y 17 registros (`Register._count` = D0-D7, A0-A7, SR); descifrado del plugin (`debugAdapter`/`extension.js`) y portado a `profile-samples.mjs` (agrega por **ciclos**, no por muestras; modos `--per-frame` y `--frame N`). **(c) EFECTO OBSERVADOR (2026-10-18): con el profiler activo la 303 corre PERFECTA** (40 y 100 campos, todos a 142.102 ciclos = 1 campo, 81% en el `Wait()` de Exec) — la pérdida no se reproduce bajo observación. **(d) CIERRE PARCIAL (2026-10-18): NO es del entorno — la 302 pierde ~1-2%** (contador `vbl` = `os::frame_count`: 402 latidos en 10 s de pared con `fps10=488` estable en dos ventanas) **y la 303 pierde 8-12%** (444 fps reales vs 488 de la 302); excluidos por medida: coste del HUD/demo, camino de espera (polling igual/peor), preempción (`vb_foreign=0`), IRQs (`vb_seq` 1:1) y Blitter (`hw_calls=0`). **La causa raíz queda no identificada** tras agotar la resolución de las herramientas actuales (el profiler la vuelve perfecta; los contadores la ven intermitente por rachas en el atract). Característica específica de la 303, no del entorno; la 303 queda cerrada con la nota en su `VALIDATION.md`. Reabrir solo con una herramienta que observe la espera de VBlank sin alterarla (p. ej. traza de `Signal`/`Wait` en el emulador).

**Evidencia nueva (2026-10-19) — el síntoma NO es exclusivo de la 303; familia «el bucle deja de producir frames»:** durante la verificación del ciclo de tema de la **306** (`306_hud_crpg`), el contador de frames del motor (`app.frame()` leído por el periférico) queda **clavado en 262 de forma determinista** en dos corridas independientes (settle 10 s y 24 s: mismos valores exactos en todos los contadores), con `run-demo` terminando en `ok`: el bucle `App::run` duerme en `Wait()` y **no vuelve a consumir el latido** (no es salida limpia: `run_demo` corre indefinido). Consecuencia inmediata: el **ciclo de tema/skin de la 306 (cada 300 frames) nunca se dispara** en el entorno actual — su efecto visual está **SIN VERIFICAR en vivo** (el estado por defecto sí está validado). Reframe de DT-035: el «vídeo congelado» de las capturas por paso es, al menos en parte, **este mismo estancamiento de la app** (el emulador sigue vivo y sirve el último frame), no solo un artefacto del runner. Hallazgo operativo del runner: la elección de emulador es **`WINUAE_GDB_DIR`** (prioridad: override → `../WinUAE-DBG/bin` → extensión; `WINUAE_PATH`/`AMIGA_WINUAE_PATH` no la afectan), y el binario del fork del repo **quedó roto a mitad de reconstrucción por el hilo paralelo** (`Process exited with code -1` en el lanzamiento) — usar `WINUAE_GDB_DIR=<extensión>/bin/win32` mientras dure. Plan: (a) instrumentar la espera (`Signal`/`Wait` o contador en el ISR) para distinguir «latido perdido» de «señal perdida en Wait»; (b) reproducir el estancamiento de la 306 en aislamiento (sin mis ediciones de skin, build previo) para descartar regresión de la demo; (c) retesteo del ciclo de tema con la demo corriendo >300 frames.

**RESUELTO para la 306 (2026-10-19, misma sesión):** es la **familia 212/DT-011** («el SO vivo apaga el latido en el bucle por señal»), confirmada por el hilo paralelo en la 001 y cruzada en `212-vblank-irq-apagada-por-el-so.md`. Fix aplicado: **espera de VBlank activa** (`eng::amiga::VblankWait::Active` en `run_demo`, que configura `set_vblank_sleep(false)` antes de `start`; latido por IRQ + sondeo de VPOS). Verificado: la 306 pasa de frame clavado en 262 a **825 frames en 16,5 s** (sin estancamiento), el **ciclo de tema/skin se dispara** (contador `iconset`=2) y la secuencia lo captura (iconos rojo→blanco, verbos LOOK→TAKE→USE, fps 43,9→45,0). **Implicación para la 303 (DT-036):** su pérdida de 8-12% («el bucle queda fuera 2-5 campos») es el **mismo mecanismo en grado leve**; siguiente paso = cambiar la 303 a la espera activa y re-medir con el reloj de VBlank.

**RESUELTO también para la 303 (2026-10-19) — y la espera activa NO basta para su clase:** medida con el ratio `frame`/`vbl` (reloj de VBlank de 50 Hz exactos; contadores permanentes 2/3 de la demo, patrón del `vbl` de la 302):
- **Señal** (lo anterior): 659/747 = **88,2%** (11,8% de latidos perdidos).
- **Activa** (`VblankWait::Active`): 209/234 = **89,3%** y la app **muere** a los ~4,7 s — **valores idénticos en dos corridas** (parón determinista; el latido IRQ muere con la app). Es la misma clase que la 119 y el juego 400: la espera activa evita el camino de `Wait()` (donde la 001/306 perdían el despertar) pero **no** la interacción de las IRQ de nivel 3 con exec bajo el SO vivo (los tres caminos de IRQ del doc 212 mueren; el sondeo es el único estable).
- **Sondeo** (`VblankWait::Polling` → `App::run_polling`, nuevo valor del enum en `run_demo`): **99,8-99,9%** (635/636, 843/844) y la app sobrevive la ventana. **DT-036 cerrada para la 303.** El `fps10` de pared (400-419) subestima; el ratio `frame`/`vbl` es la medida honesta y el gate de la demo ahora lo exige (`analyze-sequence.sh`: `vbl >= 500` y ratio `>= 950/1000`).
- **Nota de clase (modelo de tres niveles)**: señal → letal bajo SO vivo (guard `0xE212` del engine la rechaza); **activa** → resuelve la clase «despertar perdido» (001/305/306, verificadas); **sondeo** → la única estable para la clase «muerte por IRQ bajo SO vivo» (119/400/303). Elegir según la clase observada; el doc 212 es la referencia.

## DT-039 - `bus_budget.hpp`: division u64 -> `__udivdi3` (landmine de enlace con `-nostdlib`) - RESUELTA (fix directo)

**Sintoma:** el enlace de cualquier demo que instancie `eng::hw::amiga500_bus_budget` falla con `undefined reference to __udivdi3` cuando el compilador NO elimina el calculo muerto de `remaining_pct10` (p. ej. al cambiar el inlining de `App::run`): el enlace de demos usa `-nostdlib` y `support/gcc8_a_support.s` solo aporta las rutinas de 32 bits.

**Fix:** division en **u32** (el cociente cabe: peor caso `remaining <= 227x312x50` y `x1000 < 2^32`), documentado en el propio codigo. Si algun dia hiciera falta u64, habria que aportar `__udivdi3` en `support/`.

## DT-040 - Fragilidad de codegen del toolchain m68k: `arith.hpp` "asm operand requires impossible reload" y sensibilidad de inlining - ABIERTA (familia DT-038)

**Sintoma:** con un cambio de inlining en el TU de la demo (`App::run` llamando directo a `run_frames_polling`, un solo parche de una linea), gcc 15.1 m68k falla al compilar `eng::math::arith<short>::mulu` (`engine/include/eng/cpu/m68k/arith.hpp:42`): `'asm' operand requires impossible reload`. Con el codigo anterior compila; es presion de registros/decisiones de inlining, no un error del codigo fuente. Segunda instancia de la familia DT-038 (miscompilacion/fallo de codegen por flags/contexto del TU de la demo).

**Plan:** (a) reproducir minimo (TU + flags) y reportar upstream; (b) mientras, evitar `DEMO_OPT` agresivo y cambios de inlining gratuitos en TUs de demo; (c) si reaparece, probar `-O2` (ya por defecto) y `-fno-*` del barrido DT-038.

## DT-037 - Demo 080: el render de la sala salio roto durante la sesion 2026-10-18 - RESUELTA (era miscompilacion -O3, DT-038)

**Sintoma:** con la camara congelada en pan=0 (-DK080_PAN_FREEZE=0; captura estatica sin tearing) el frame muestra triangulos/diagonales a medio rellenar y regiones invertidas (patron en X): no es el tunel simetrico esperado (techo arriba, suelo abajo, paredes en cuna, pared del fondo centrada). El sintoma arrastraba desde las primeras descripciones de vision («un triangulo verde (pared izquierda)») sin tratarse como defecto: las validaciones de la demo comprueban el *mecanismo* (contrato de desplazamiento de ventana, §1-§2 de su VALIDATION) y no la correccion de la escena.

**Evidencia (acotado):**
- **Referencia CPU limpia**: el repro host de la misma escena (mismas caras/colores, mismo project_perspective + clip_polygon, ill_polygons_by_plane_cpu) produce el tunel correcto -> geometria, proyeccion, recorte y paridad por plano estan bien; la divergencia esta en el **camino Blitter** (contorno EOR ONEDOT + area fill FILL_XOR) con esta configuracion de caras.
- Primer experimento (sesgo de z en esquinas compartidas del tubo, como el de los paneles, +-4): **sin cambio** (el sesgo del extremo lejano es sub-pixel: +-4 unidades a z≈1024 ≈ 0,1 px).
- La ficha de la tecnica documenta que las aristas EXACTAMENTE horizontales en pantalla las descarta el trazador EOR (line_prepare_impl); las caras del tubo (suelo/techo/fondo y los bordes near/far) las tienen de forma estructural, ademas de aristas compartidas entre caras de planos distintos.

**Cierre:** biseccion por commits con git worktree: el commit original (1b32ab92) renderiza limpio y 45a056e9 rompe; la variable es -O3 en el TU de la demo (mismo codigo a -O2 limpio) -> **miscompilacion** (DT-038), no logica. Arreglo en esa version del codigo: DEMO_OPT=-O2, verificado por captura estatica y por el pase C de vision contra el INTENT (VALIDATION 9). El par de flags vigente hoy es `-O3 -fno-tree-vectorize` (ver DT-038: la ventana de flags depende de la version). Leccion de metodo (que la vision no cazo el sintoma): DEMO_VISUAL_DEBUG 5.1 y el pase C obligatorio en 6.4.

## DT-038 - -O3 en el TU de la 080 rompe el render (vectorizador de bucles de gcc 15 m68k) - RESUELTA (workaround; bug upstream pendiente)

**Sintoma:** con DEMO_OPT=-O3 (en la TU de la demo; el engine queda en su optimizacion por defecto) el render de la sala 3D sale partido (triangulos/patron en X) en todos los paneos; con -O2 el mismo codigo renderiza el tunel correcto. No es un problema del fill ni de la geometria: la referencia CPU de la misma escena (host) es correcta y la biseccion por commits lo aisla al cambio de bandera.

**Evidencia:**
- Biseccion con git worktree (arbol aparte): 1b32ab92 limpio; 45a056e9 (que anade -O3) roto; 45a056e9 con DEMO_OPT=-O2 limpio. Capturas en la sesion (sequence de cada build).
- El sintoma afectaba a las salas (planos 0-1) y no a los paneles (planos 2-3) en el build roto; en una variante (caja de sala desactivada a -O2) aparecieron rayas en los paneles: la firma es de **parametros de Blitter erroneos generados por codegen**, no de logica (los valores dependen del codigo compilado).
- **Metodo del barrido**: paneo congelado (frame determinista) + md5 del PNG de la secuencia por variante de flags; el "correcto" se ancla contra `-O2`.
- Coherente con la nota del toolchain: el codigo de la demo usa aritmetica Fixed/s16 intensiva y helpers inline del engine; -O3 (mas agresivo con alias/orden) sobre el TU de la demo es el sospechoso.

**Causa raiz (2026-10-18, barrido de flags con captura congelada determinista)**: el **vectorizador de bucles** de gcc. Con la camara congelada en pan=0 (`-DK080_PAN_FREEZE=0`) el render es determinista (md5 del PNG fijo): correcto a `-O2` (`9da58872...`), roto a `-O3` (`789708e7...`); `-O3 -fno-tree-vectorize` vuelve al md5 CORRECTO, mientras que `-fno-tree-slp-vectorize`, `-fno-gcse`, `-fno-tree-pre`, `-fno-tree-dominator-opts` y `-fno-web` siguen rotos (algunos con firma distinta, `222b70b2...`). En m68000 no hay SIMD: la vectorizacion no aporta y puede miscompilar.

**Workaround vigente (2026-10-19):** el par **`DEMO_OPT=-O3` + `DEMO_FLAGS=-fno-tree-vectorize`** en `build.args` de la 080, certificado con captura congelada a pan 0 (md5 idéntico con y sin culling). La ventana de flags **se ha desplazado con el código**: con la versión actual `-O2` renderiza la sala partida (bandas/diagonales) y `-O3 -fno-tree-vectorize` es el par correcto; la atribución al vectorizador se confirmó en la versión anterior (`-fno-tree-vectorize` corregía el md5 a `-O3`). Conclusión operativa: el codegen del TU es frágil; **cualquier cambio de flags o de versión del código exige repetir la captura congelada a pan 0** antes de dar el render por bueno. Aviso preventivo en el comentario de flags de `tools/build/build-demo.sh`. Bug de gcc pendiente de reporte upstream.

**Plan:** (a) comparar el asm de -O2 vs -O3 de las funciones calientes del TU (bucle de caras, litter_line_eor_prepare inline y los helpers de fill) para localizar el punto; (b) si es un bug del compilador, registrar la bandera/version y evitarla en los uild.args de demos (documentar la comprobacion visual obligatoria cuando se active -O3 en un TU); (c) revisar las demas demos con DEMO_OPT=-O3 (p. ej. 116) con el pase C de vision.

## DT-043 - 080: sala invertida con yaw positivo - RESUELTA EN EL ALCANCE MEDIDO (causa raíz no aislada)

**Síntoma:** con la cámara congelada en pan=+13 (`-DK080_PAN_FREEZE=13`) la sala muestra el **suelo (marrón) arriba** y el **techo (azul) abajo**, con las paredes como cuñas invertidas; los **paneles (planos 2-3) están correctos**. A pan 0 y −13 el render es correcto. En la corrida dinámica el defecto aparece al cruzar a pan positivo (capturas a PAN +13/+43 con la sala «desplegada»). El modelo de visión (`qwen3-vl:8b`) describe la imagen invertida como **coherente** («suelo abajo»): el eval sin contraste no vale como veredicto.

**Acotado:** NO es del rebanado F2 ni del refactor por etapas: el build de **HEAD (`cb5eaaaf`, `render_full` monolítico) produce la MISMA imagen** a pan +13 (diff 0 px contra el build por etapas) y su pan −13 es correcto. Afecta a las caras de la sala (planos 0-1); los paneles no. Usar la caja completa del FB en vez de 8..200 no lo cambia. El defecto llevaba oculto porque las capturas congeladas de certificación eran a pan 0 y el eval de visión lo racionalizaba.

**Bisección (2026-10-19):** con `-DK080_CPU_FILL` (mismas caras, mismo clip, relleno CPU) el +13 sale **correcto** ⇒ geometría/clip bien; el defecto está en el **camino Blitter** y solo en el **plano 1**. Descartados por A/B el **dedupe de aristas** (misma imagen rota al desactivarlo) y los **vértices duplicados** (VER=40, ninguno). El volcado de la chip RAM por canal lateral devuelve ceros en la zona de la escena (el programa sí se lee): usar memwatch/GDB o la consola 0xB70000 para el volcado fino.

**Verificación de cierre (2026-10-20–21):** el síntoma histórico no se reproduce en yaw a `K080_PAN_FREEZE=13` y `65`; el suelo queda abajo y el techo arriba, con capturas pixel-idénticas culling ON/OFF (`MAD=0`). La secuencia de 128 updates anterior pasa el contrato Full/Fake. Para los barridos nuevos ±60°, el culling yaw-only se desactiva fuera de `|pan|≤65`: en el ángulo +60° el camino culling habilitado antes descartaba superficie visible y mostraba solo una franja; con el fallback de clipping general la pared entra al encuadre. Yaw/pitch puros y Lissajous pasan secuencias de 32 updates y el ciclo integrado muestra el frustum en ambos sentidos. HOST-479 coincide temporalmente con la mejora de clipping, pero la causa raíz histórica no está aislada; el alcance funcional nuevo queda descrito en `080_camera_pan_window/VALIDATION.md` §§13–14. Flicker/tearing y cadencia objetivo siguen abiertos en DT-034.

## DT-041 - `clip_edge` desborda con tramos de arista > s16 (cruce basura lejos del real) - RESUELTA

**Sintoma:** tras corregir la arista de cierre del clip (HOST-479), la demo 080 quedaba clavada en los primeros frames (pantalla plausible pero congelada; run-status `frame` fijo y ~5 s de emulación por frame).

**Causa raíz (sonda host A/B + fuzz de 2 M cuadriláteros):** `clip_edge` calculaba `dx`/`dy` como `s16`; una arista con un extremo «basura» de proyección (un vértice con z≤0 en la 080) dista hasta 65535 y el tramo envolvía (p. ej. `dy=60000` → −5536), produciendo un cruce basura (x=222 donde el real era ~340). El clip viejo **no calculaba la arista de cierre** y evitaba por accidente esos cruces; el fix de HOST-479 los calcula (correcto) y destapó el desbordamiento. El fuzz acotó el máximo de vértices por pasada a 10 (cabe en los buffers de 10 de la 080): no había desbordamiento de buffers.

**Fix:** `clip_interp` (`lib2d.hpp`): vía rápida idéntica a la clásica cuando `d`/`n`/`k` caben en s16; camino ancho cuando no (ratio `|n/k|` en 16.16 con división u32 y producto u32 acotado, saturación final; sin rutinas de 64 bits, que `-nostdlib` no aporta: DT-039).

**Verificación:** sonda host A/B (caso con `dy=60000`: de `(222,0)` basura a `(340,0)` real; salida idéntica con la implementación u32 y con una intermedia de 64 bits); HOST-479 ampliado con comprobación de hull y dos casos span>s16 que fallan con la versión intermedia y pasan con el fix (7/7 verde); la 080 deja de clavarse en frame 4 y avanza hasta el atasco de DT-032. Alcance: el clip queda cubierto para cualquier entrada s16; el estado final estable de la 080 depende de DT-032.

## DT-042 - Coste del HUD por palabra y parones por carrera de despertar del VBlank en la demo 001 - RESUELTA

**Síntoma inicial:** el redibujo de las dos líneas de la HUD de la demo 001 hundía la cadencia (de ~49 fps sin HUD a ~10-17 fps) y algunas variantes con MENOS trabajo por frame se paraban del todo (frames congelados, ciclos de CPU parados). Las medidas parecían contradecirse entre variantes (42 vs 17,6 fps con los mismos stores; 0,1 fps con el HUD por diferencias).

**Causa raíz (dos capas):**
1. **Carrera de despertar del VBlank** en el bucle por señal puro (`run_frames`): en ciertas fases del frame se perdía el despertar y la tarea quedaba dormida para siempre (misma clase que `212-vblank-irq-apagada-por-el-so.md`). El umbral dependía del trabajo por frame, así que TODAS las medidas de la investigación estaban contaminadas por parones de tasa variable: eso explica la contradicción 42 vs 17,6 (fases distintas ⇒ distinta tasa de pérdidas) y el 0,1 fps del HUD por diferencias.
2. **Coste real del HUD**: redibujar las dos líneas completas cada frame cuesta más de un campo de CPU (cadencia cuantizada a 25/16,5 fps aun sin parones).

**Fix aplicado:**
- `backend.set_vblank_sleep(false)` + `run_frames` (**espera activa**: latido por IRQ + sondeo de VPOS), la variante recomendada del engine para la clase de fallo de 212; el bucle deja de perder despertares.
- Primitiva general `Playfield::store_plane_words_row` (trozo de línea, mapeo una vez; `Surface::draw_text_words` compone en trozos de 8 palabras) y **repintado por diferencias** en la demo (solo las 1-2 palabras que cambian por frame).
- Fixes previos asociados: BOM de `build.args` (herramienta), servicio de Blitter de fondo OFF (carrera con blits síncronos), clamp de la ventana de los bobs (escrituras fuera del plano).

**Verificación (A500_debug, `measure-fps` sin warp):** ventanas estables **48,0-49,5 fps**, ~1,0 campos/frame; flicker `OK` (`max_candidates: 0`); visión canónica OK (`demos/features/adventure/amiga/001_adv_walk/src/VISION.md`). La demo A0 queda **VERIFICADA** (`VALIDATION.md`).

**Nota de método:** toda medida de cadencia de demos con `run_frames` debe declarar el modo de espera de VBlank; los parones por despertar perdido falsean medias y comparaciones A/B (se vieron ratios de 2,4x entre variantes equivalentes).

## DT-044 · Demo 088: render 3D con cadencia inestable y presupuesto de CPU agotado — ABIERTA

**Síntoma y medida actual:** la secuencia animada avanza y el barrido objetivo verifica cuatro bordes, cuatro esquinas y HUD limpio. La ruta aceptada D+G (`K_CLIP_PROJECT_BEFORE_SCREEN_CLIP=1`, contador por buckets de 8 frames) mejora de **31,91 fps / 222.296 ciclos/frame** a **42,96 fps / 165.132 ciclos/frame** (+34,6 % fps, −25,7 % ciclos), pero las ventanas 20 s siguen oscilando entre **38,52 y 47,17 fps** (≈1,164 fields/frame). El suelo de 25 fps para relleno se supera, pero la cadencia irregular impide marcar la demo **VERIFICADA** para movimiento fluido.

**Atribución:** WinUAE sigue mostrando **142.102 ciclos CPU ocupada por field, 0 libres**. En la build D+G las muestras atribuyen el coste restante a `clip_face_to_rect`, `fill_polygons_by_plane_patterns`, `wait_blitter` y clear/scratch; el HUD ya bajó `draw_text_words` de 27,5 % a 0,4 % de muestras. Se probaron y retiraron por falsación el borrado de scratch, el fast path de cara completamente interior, el salto de pasada 2D, la omisión near, la cota estática de near y la omisión de contención global; el último caso dio un delta de 2,7 % con control GDB no reproducible, bajo el umbral acordado. El fast path de contención por plano 2D tuvo dos ventanas largas donde el contador se detuvo, así que no se conserva.

**Correcciones y verificación geométrica (alcance probado):** el corte de una arista fija dependía del winding porque `cut_edge` recibe ambos sentidos entre caras vecinas; HOST-481 comprueba ahora la simetría con `Fixed<s16,0>`. El mismo test escala/traslada un pentágono por los cuatro bordes y compara un tetraedro parcialmente recortado con la referencia CPU por plano. HOST-481, L1-003, L1-005 y L1-005 `--edge-stress` pasan; la pose f0473 queda dentro de ±2 px frente a CPU. `verify-088-sequence.mjs` valida 32 muestras de cuatro bordes/esquinas, presencia de los tres colores y cero píxeles rojos/azules en HUD; en 8 frames consecutivos comprueba 7/7 cambios. Flicker-check da cero candidatos y Ollama no ve artefactos en el tramo consecutivo; una evaluación espaciada señaló una línea pequeña no reproducida en el diff por pares. La visión sigue siendo auxiliar frente al gate por píxel.

**Plan:** continuar P5 de [`ROADMAP_3D_BLITTER.md`](ROADMAP_3D_BLITTER.md) con campañas A/B de una sola variable en las pasadas de `clip_face_to_rect`/relleno por plano, conservando la comparación CPU y los gates secuenciales. No declarar la demo verificada hasta lograr cadencia uniforme; si la CPU A500 no alcanza 50 fps, decidir y documentar una modalidad estable que cumpla el suelo de 25 fps para polígonos.
