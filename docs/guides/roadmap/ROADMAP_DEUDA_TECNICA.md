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

## DT-021 · Demo 213: `A500_release` no alcanza READY (init, reserva Chip)

`A500_release` de la 213 falla en init con `detail=0x06CBF005` (`mark_mem_failed` del grupo sprite/música, banco Chip con ~191 KB libres). Reproducible en el engine sin los cambios del turno. Detalle y evidencia: `docs/debugging/investigaciones/213_release-strict-aliasing.md` §Reapertura.

**Causa raíz y fix (2026-10-16).** El Chip queda fragmentado por la carga del ejecutable: el mayor bloque al arrancar el juego es ~236 KiB en release (vs ~428 KiB en debug), y `game_memory_fit_available` restaba un headroom **fijo** de 128 KiB → pool de ~108 KiB para una necesidad de ~110 KiB (escena 50 KiB + assets img 50 KiB + bob/mod/copper). Fix:
**el headroom de Chip escala con el bloque** (1/4, suelo 64 KiB, tope 128 KiB) en `memory_profile.hpp`; con 236 KiB el pool pasa a 172 KiB y la reserva de assets ya no da `NoSpace` (HOST-394 cubre el caso). **Pendiente**: con el pool arreglado la release avanza y queda **colgada en init** (`state=InitStarted`, PC estable en `MemBank::configure`, `debuggerState=2`) — verificado que **no** es el fix de espera del Blitter (persiste con `amiga_blitter.cpp` sin él). Siguiente paso: adjuntar GDB con `WINUAE_GDB_PERSIST_LISTENER=1` y hacer step/backtrace del cuelgue (la herramienta está en reparación por DT-023), y probar la hipótesis de **stack** (128 KiB del runner) subiéndolo a 256 KiB.

## DT-022 · Blitter: plano 0 perdido al mezclar copias y racha cookie-cut en un `FramePlan`

Síntoma: en la demo 121 el actor pierde el **plano 0** de forma intermitente y posicional cuando el plan mezcla 5 `CopyRect` (restaurador) y una racha de 5 `MaskedBobCookieCut` (actor); invisible si el plano 0 del fondo vale 1. Evidencia: `detail=0x1F2102` (auto-chequeo, sub=0, body=30) y `detail=0x1E01252F` (guarda por frame, x≈260, body=30 ≥3 frames). Actor-only nunca falla; dos `execute_frame_plan` separados = 6/6 limpios. Detalle: `docs/debugging/investigaciones/121-actor-plano-0-y-carencias-blits.md`. Acción: test host de la secuencia mixta de registros (extiende HOST-451) y, con repro, fix del motor; mientras, la demo usa planes separados. Relacionado: el contrato de sincronía/verificación de lectura y el validador de geometría de anillo propuestos en el informe.

**Motor corregido (2026-10-16) — contrato de escritura con el Blitter ocupado.** El mecanismo encaja con la fuente primaria: el AHRM prohíbe escribir registros del Blitter con un blit en curso («Multitasking and the Blitter») y WinUAE reinicia el blitter si se escribe `BLTSIZE` con uno activo (`blitter.cpp:2036/2058/2101`; ficha `docs/reference/emulators/winuae/blitter-busy-writes.md`). El plan encadenaba jobs sin esperar (solo «funcionaba» por timing del bus) → en un burst tras un blit largo se pisa el estado del que corre. Fix: `submit_blit_job` espera `BBUSY==0` antes de programar cada job (las rachas `BlobBatch` ya esperaban por objeto). Coste medido en la 213: nulo dentro del ruido (sync 32,30 vs 32,20–32,32 fps; async 27,41 vs 27,58). Reproducción en la demo pendiente de cierre: el A/B local con plan único quedó **contaminado por DT-023** (instancias huérfanas/step-capture); la verificación limpia es la herramienta golden de la 121 (`K121_GOLDEN_ACTORS` + `tools/analyze/verify-121-actor-golden.mjs`). Queda: re-ejecutar el plan único con esa verificación y, si sale limpio, retirar la mitigación de dos planes de la demo.


**Verificación en la demo 121 (2026-10-16)**: con el contrato en el motor, la demo volvió al **plan único** por frame (restaurador + actor juntos; `K121_TWO_PLAN_SPLIT` queda como A/B): 7/7 arranques limpios con telemetría viva y medida estable (47,67 fps / 148,8k ciclos, igual que con dos planes: el coste era espera, no trabajo extra). El banco golden (`K121_GOLDEN_ACTORS` + `verify-121-actor-golden.mjs`) pasa 10/16 subposiciones exactas en multi-plan con máscaras por slot (el resto y ~127 px de colateral quedan en el banco como anomalía residual, ver el informe 121).

## DT-023 · Herramientas: capturas de secuencia a mitad de frame y `--config` ignorado

En `tools/run/run-demo.ts`: (a) `--sequence-step-frames` pausa en el `ready-probe` y captura estados sin los blits del frame reflejados (actor ausente o con motas) — indujo diagnóstico falso de render en la 121; **re-verificado 2026-10-16**: 20/20 frames del step-capture de la 121 con el actor ausente en posiciones donde la captura simple sí lo muestra (x=78 bajo copa es legítimo, x=56..75 no lo es) → el stop del probe sirve un estado previo al dibujo del frame; (b) ~~`--config <id>` se ignora silenciosamente~~ (**corregido en el análisis del 2026-10-16: `--config` funciona; escribe capturas/report en `out/run/<demo>/<CONFIG_ID>/` y leer el directorio `A500_debug` indujo a error — documentar el directorio por config y avisar en consola al forzar `--config`**); (c) el runner a veces termina con `Process exited with code null` tras READY sin escribir report — y una instancia huérfana de un run anterior puede quedar escuchando en los puertos y servir su estado VIEJO al siguiente run (fallos idénticos repetidos con el mismo `detail`, observado 2026-10-16: `0xA1D2108` en arranques consecutivos; 9/10 limpios tras higiene de procesos antes de cada run). Detalle: `docs/debugging/investigaciones/121-actor-plano-0-y-carencias-blits.md` §Carencias de las herramientas. Acción: capturar tras `wait_blitter`/frame completo, hacer efectivo `--config`, y robustecer el cierre del runner.

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
- [ ] Investigar el atasco de la cadena de espera de VBlank con cadencias mixtas (repro: 001_rpg_dungeon con HUD compuesto 1 de cada 25 frames; ver `dist` de la demo en `VALIDATION.md`), en la línea de `vblank-timer-inconsistencies.md`.

**Evidencia:** `demos/features/rpg/amiga/001_rpg_dungeon/VALIDATION.md` (tabla de medidas y ablaciones); código y comentarios de coste en `src/main.cpp` de esa demo.

## DT-026 — Demo 116 (`flatshade_convex`): 19,7 fps (< suelo 25) y optimizaciones que exigen el referee async

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

**Evidencia:** `demos/techniques/amiga/effects/116_flatshade_convex/VALIDATION.md`; notas de coste en `src/main.cpp` de 079/116; perfiles en `out/debug/*` de la campaña.

## DT-027 — HUD ligero obligatorio al medir rendimiento (recomendación para hilos futuros)

**Origen:** campaña 079/116 (2026-10-17) y hallazgo previo de la RPG 001 (DT-025).

**Lección medida:** un HUD de texto dibujado **por frame** contamina la medida de fps: 3 líneas de 40 caracteres en 4 planos ≈ **750k ciclos/frame** (DT-025) y un prototipo de franja pintada cada frame en los 5 planos de la 079 ≈ **205k ciclos/frame** (medido por ablación A/B: 17,45 vs 36,27 fps). El HUD de **zona aparte con 1 bpp** y actualización **solo al cambio** mide **0 ciclos/frame** (las 079/116 modernizadas lo usan y mantienen su fps íntegra).

**Regla para futuros hilos** (amplía §0 de `AGENTS.md`, que exige el HUD más sencillo posible):

- Al **medir rendimiento** (y en general), el HUD va en **zona aparte** de la pantalla con **1–2 bitplanes propios** activos **todo el frame** (`BPLCON0` + puntero en la cabecera de la lista), **nunca** repintado sobre los planos del efecto ni por frame.
- **Prohibido cerrar el HUD con una «zona» de WAIT `>255`**: el comparador del Copper usa semántica `>=` sobre `V[7:0]` (`custom.cpp:4416` `coppercomp`), un WAIT por encima de la 255 dispara en la raster 255 y **no se puede volver a esperar nada después** dentro de la lista: el plano extra queda encendido más allá de su banda (franjas de basura; pasó en 079/116, 2026-10-17).
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

**Pendiente (no bloquea):** validación genérica de `BlitJob` en la alta (`BoundTarget`), contrato de tamaño de la hoja del BOB, canarios de capacidad en `SpriteManager`/`copper::Plan`/`TileScrollDriver`/audio y barrido de `Block::view.data()` en demos técnicas. Plan en `ROADMAP_API_COHERENCE.md` §F8 y `ROADMAP_MEMORY_OWNERSHIP.md` Fase 8.

## DT-029 - Demo 011 (`typed_memory`): no alcanza READY (state=0, `PC=0x4`) - ABIERTA

**Síntoma:** `bash ./tools/run/run-demo.sh demos/techniques/amiga/setup/011_typed_memory` agota los 40 s con `state=0`/`detail=0x0`. En vivo (canal lateral) el CPU queda en `PC=0x4`, `SR=0x14`, señal de un salto a dirección inválida antes de que `reset()` del run-status llegue a ejecutarse.

**Alcance:** se reproduce **también** con el `entry.hpp` de HEAD (que define y asigna `SysBase` en `main`), así que no lo introduce el bootstrap de plataforma de este hilo. `122_circles` y `213_bartman_abyss` (mismo backend) arrancan bien, de modo que no es un fallo global del backend.

**Siguiente paso:** depurar con GDB/MCP el primer `PC` anómalo de la 011 (revisar la cadena `App::run`/heartbeat, que `bd27c2b7` tocó en `engine.hpp`) y añadirlo al barrido de flota.

**Evidencia:** salida de `run-demo` (timeout de READY, `state=0`) y lectura del canal lateral (`state` con `pc=0x4`, `sr=0x14`) en sesión MCP sobre `out/run/011_typed_memory/A500_debug/runner.uae`.
