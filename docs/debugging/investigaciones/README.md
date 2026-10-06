# Investigaciones y hallazgos

Hallazgos concretos de depuración: **bloqueos abiertos**, **post-mortems/lecciones** y **consultas a IA externa**. Son bitácoras (narran el proceso, lo descartado y las hipótesis); el estado vigente del engine está en `docs/engine/` y las rarezas del emulador en `docs/reference/emulators/`.

## Abiertas (bloqueos vigentes)

| Documento | Descripción |
|-----------|-------------|
| [octamed-startmusic-hang.md](octamed-startmusic-hang.md) | **A1 (parcial/operativo)**: con `READY_FRAME=0` la música **suena** y `_startmusic` retorna; el modo que **espera frames** se cuelga (el playroutine necesita su timing por IRQ y el bucle de espera lo atropella). Incluye mecanismo, descartes, el probe GDB (`gdb-probe-octamed`) y la lección de proceso [`LECCION-CONTEXTO-DE-LA-FUENTE`](../../guides/methodology/LECCION-CONTEXTO-DE-LA-FUENTE.md). |
| [pump-timer-o1-codegen.md](pump-timer-o1-codegen.md) | **Mini-SO**: `os::add_timer` con periodo > 1 no entrega los `Timer` al `App` a **`-O1`** (perfil `--debug`); a `-O2` funciona. Bug de codegen de gcc 15 m68k en el camino inlineado `run_frames_polling`→`update`→`pump_messages`→`on_frame` (el `this` de `on_frame` es erroneo). Incluye el aislamiento, los workarounds descartados y la mitigación `DEMO_OPT=-O2`. |
| [captura-negra-intermitente-050-051.md](captura-negra-intermitente-050-051.md) | Las demos 050/051 producen a veces una captura 100 % negra pese a READY. No es el fuente (051↔050), ni el config (ruta `dh1`), ni los blits; es intermitente/por estado. Siguiente paso: leer registros reales por GDB. |
| [scene-rebuild-efectos.md](scene-rebuild-efectos.md) | Migrar una demo al modelo de efectos con **reconstrucción por frame** del `Scene` descoloca la copperlist (la demo validada se revirtió). Hipótesis y cómo atacarlo (test host comparativo). |
| [winuae-pantalla-negra-arranque.md](winuae-pantalla-negra-arranque.md) | Pantalla negra al arrancar WinUAE (el sistema no botea). |
| [diagnostico-adf-negro.md](diagnostico-adf-negro.md) | El ADF se queda en negro (diagnóstico). |
| [diagnostico-depurador-f5.md](diagnostico-depurador-f5.md) | El depurador no se lanza con F5 (diagnóstico). |
| [pending-verification.md](pending-verification.md) | Repaso pendiente de generalidad de interfaces y modelado del engine. |
| [memory-ownership-inconsistencies.md](memory-ownership-inconsistencies.md) | **MEM-001..MEM-011**: divergencias abiertas entre `MemoryManager`/arenas, pools persistentes, caché de assets, ownership y documentación; MEM-009 añade las leases por nivel de `FramePlan`/Copper y la demo SFX 281 pendiente de ejecución Amiga. |
| [vblank-timer-inconsistencies.md](vblank-timer-inconsistencies.md) | **TIME-001..TIME-010**: duplicación del VBlank entre latch y FIFO, modos de sincronización ausentes e integración incompleta/deriva de timers. |
| [300_compositor-una-sola-ventana.md](300_compositor-una-sola-ventana.md) | Demo 300 (compositor GUI): la captura de **un solo frame** mostraba solo la ventana B. Con **secuencia larga** se ven las tres: no había defecto de composición. El "parpadeo" percibido era del **movimiento** (saltos de 16 px + frames congelados), corregido a 1 px/frame. Lección: capturar secuencia + **frame-diff determinista**, no fiarse del modelo de visión. |
| [minios-demo-captura-estatica.md](minios-demo-captura-estatica.md) | Las demos del mini-SO con `run_frames_polling` (206/209/212/213) quedan **congeladas en el frame 0** en la captura automática (secuencia idéntica; el input no llega). Pre-existente (confirmado con `git stash`) y **no** una regresión de M12. 215 (bucle `run_frames` con copperlist real) sí captura movimiento. Sospecha: `debug()` (overlay host-side) vs display real. |
| [risky-woods-208-sprite-scroll.md](risky-woods-208-sprite-scroll.md) | **Demo 208 (bloqueado)**: fondo de sprites estilo *Risky Woods* (6 canales, 320 px, scroll 1 px/frame). Fija los **hechos de hardware verificados** en la fuente de WinUAE (coincidencia de X que dispara el dibujado, `SPRxCTL` desarma, fetch solo al inicio de línea, terminador `0,0` → `VSTART=0`), los errores concretos de los intentos previos y un **plan de reimplementación por etapas** (E0–E5). |

## Hallazgos y lecciones (cerrados)

| Documento | Descripción |
|-----------|-------------|
| [p61-audio-dma.md](p61-audio-dma.md) | **213 / P61 resuelto**: el DMA de audio no se encendía. Dos causas: (1) el playroutine (`p61system=0`) difiere el encendido a la IRQ de CIA-B (nivel 6), que el `App` no atiende → el engine aplica `_P61_dma` en el frame task; (2) **raíz**: el asm inline de `p61_amiga` no declaraba el clobber `d1` → corrupción del estado C++ (`m_playing`). Incluye la auditoría que descarta las rutinas de memoria recientes. |
| [audio-stream-irq-rate.md](audio-stream-irq-rate.md) | **A5 resuelto**: en `272_audio_stream` los *underruns* no eran de la IRQ (el log del emulador muestra `SETIRQ3` ≈ `looped`, **una IRQ por bloque**; el "~34×" era un artefacto de asumir 50 fps) sino del **feeder CPU-bound**. Fix: pre-sintetizar/pre-codificar la melodia una vez (`m_enc`) → `state=3`, `irq == swaps`, 0 underruns. Incluye el log de audio de WinUAE y dos bugs reales corregidos de paso (`volatile` en estado compartido con la IRQ; contadores de 32 bits que se desgarran). |
| [debug-demo-arranque-doble-texto-banda.md](debug-demo-arranque-doble-texto-banda.md) | **Resuelto**: artefactos de arranque de 060/201 — doble texto (`draw_text` sin corte en NUL), banda cian 0x0AA (sprite DMA del sistema vivo, causalidad A/B), copperlist fuera de VBL (COPJMP1) y paleta del pie. Fixes, sondas y evidencia (§11). |
| [lecciones-porte-blitter-demoscene.md](lecciones-porte-blitter-demoscene.md) | Post-mortem del Blitter de `flatshade-convex`: la raya por vértice, por qué se «normalizó» un truco de registro (`BLTDPTR`), el papel del AHRM y el checklist al importar efectos. |
| [lecciones-engine-cpp23.md](lecciones-engine-cpp23.md) | Lecciones del engine C++23: tests que escondían bugs, `-Werror=narrowing`, medir A/B, no duplicar `constexpr`, `runtime_palette()`/`apply_into`, numeración y separar referencia de bitácora. |
| [audio-debug.md](audio-debug.md) | Procedimiento de depuración de sonido: analizar la onda en host (generador/analizador), puente a C++ y verificación por canal lateral (DMACONR, registros AUDx, volcado). |
| [106_sesion-tilefield.md](106_sesion-tilefield.md) | Sesión de desarrollo: API `TileField` + demo 106 (anillo de tres tramos). |
| [112_bg-flicker.md](112_bg-flicker.md) | Demo 112: flicker de 1 px del fondo RoboCod (bitmap único); análisis y decisión. |
| [board-selfplay-and-perf.md](board-selfplay-and-perf.md) | Board games: coherencia en host y rendimiento en Amiga. |
| [213_release-strict-aliasing.md](213_release-strict-aliasing.md) | **213, resuelto**: el render de los 16 BOBs se rompía en `--release` (`-O2`) por **strict aliasing** (type-punning) del engine; fix `-fno-strict-aliasing`. El «50 fps» de release era falso (el miscompile saltaba BOBs); con imagen correcta son 38 fps. Incluye la comparación de DMA/perfil con el original. |
| [npc-table-scenarios.md](npc-table-scenarios.md) | Laboratorio de escenarios de mesa (`eng::sim` + `eng::cards`). |
| [sim-ecosystem-scenarios.md](sim-ecosystem-scenarios.md) | Laboratorio de escenarios del ecosistema (`eng::sim`). |

## Consultas a IA externa (autocontenidas)

Norma: **preguntar al modelo externo (Grok) en inglés** — responde mejor en ese idioma. Cuando la consulta se redacte primero en castellano, guardar también la versión en inglés (`*-en.md`) y pasarle esa.

| Documento | Descripción |
|-----------|-------------|
| [consulta-grok-disco-y-loader.md](consulta-grok-disco-y-loader.md) | Disco a bajo nivel (`df0:` sin Workbench, `trackdisk.device`, buffers DMA en Chip), carga `.englib`+HUNK, teclado, E/S async y ADF datos/arranque. Estado verificado, evidencia y preguntas. |
| [consulta-optimizacion-blitter-demoscene.md](consulta-optimizacion-blitter-demoscene.md) | El port C++ de `flatshade-convex` es 2.4× más lento que el original (670k vs 287k ciclos/frame) pese a los mismos blits; desglose por secciones y preguntas. |
| [consulta-ocs-attached-sprite-multiplexing-en.md](consulta-ocs-attached-sprite-multiplexing-en.md) | English consultation: can an OCS ATTACHed sprite pair be reused several times per scanline keeping the 15 colours; the exact Copper sequence (both `SPRxPOS` + head-start), and whether the high-bit loss on reuse is real OCS hardware or an emulator artefact. |
| [consulta-ocs-attached-sprite-multiplexing-seguimiento-en.md](consulta-ocs-attached-sprite-multiplexing-seguimiento-en.md) | Follow-up: hero-reuse still uneven (first ~3 periods 15-colour, the rest 4-colour), an empty gap + a stray ring, the first column renders as a solid strip, and a stray palette row between bands. Asks for reuse limits, POS write order and a known-good reference Copperlist. |
| [consulta-bplcon2-single-playfield-priority-en.md](consulta-bplcon2-single-playfield-priority-en.md) | English consultation: `BPLCON2` priority with a **single playfield** — is `PF1P` or `PF2P` the relevant field in non-dual mode; is the reset value `$24`; is `$0000` documented to put the playfield in front of all sprites; canonical value for sprites-over-playfield. |
| [consulta-bplcon2-single-playfield-priority.md](consulta-bplcon2-single-playfield-priority.md) | Versión en castellano de la consulta anterior (registro local). |
| [consulta-scroll-optimizacion.md](consulta-scroll-optimizacion.md) | Optimización de los algoritmos de scroll por tiles en A500 para **50 fps con CPU baja**: todas las variantes limited (X/Y/XY), tiles 16×16 y 32×32, pasos 1–16 px, DPF 3+3. Diseño actual (corkscrew vs anillo de tiras), medidas y preguntas concretas. |
| [consulta-scroll-optimizacion-en.md](consulta-scroll-optimizacion-en.md) | English version of the tile-scroll optimization consultation (**ask Grok in English**). |
| [consulta-scroll-optimizacion-seguimiento-en.md](consulta-scroll-optimizacion-seguimiento-en.md) | Follow-up: request for a **reference implementation** of the recommended Copper-ring + strip scroller (exact registers, ring geometry, guard invariants, XY split, 32×32, DPF 25 fps, host verification). |
| [consulta-scroll-optimizacion-seguimiento2-en.md](consulta-scroll-optimizacion-seguimiento2-en.md) | Follow-up 2: gaps in the reference (tile-bank source for the “one tall blit”, exact interleaved modulos, wrap/guard formula, streaming cost, XY split, `DDFSTRT`). |
| [consulta-scroll-optimizacion-seguimiento3-en.md](consulta-scroll-optimizacion-seguimiento3-en.md) | Follow-up 3: two OCS hardware errors in the reference — split line `0x2c+256` exceeds 8-bit VPOS (cap 208 px), and `BLTSIZE` height is 10 bits (the 1280-planeline “one tall blit” must be split). |
| [consulta-asm-flatshade.md](consulta-asm-flatshade.md) | Consulta: port ASM m68k de `flatshade-convex` (demo 116). |
| [consulta-asm-flatshade-seguimiento.md](consulta-asm-flatshade-seguimiento.md) | Seguimiento 1: aplicados los fixes, sigue negro. |
| [consulta-asm-flatshade-seguimiento2.md](consulta-asm-flatshade-seguimiento2.md) | Seguimiento 2 de la consulta ASM de `flatshade-convex`. |
| [consulta-asm-flatshade-seguimiento3.md](consulta-asm-flatshade-seguimiento3.md) | Seguimiento 3 de la consulta ASM de `flatshade-convex`. |
| [consulta-asm-flatshade-seguimiento4.md](consulta-asm-flatshade-seguimiento4.md) | Seguimiento 4 de la consulta ASM de `flatshade-convex`. |
| [consulta-freeform-scroll-blitter-en.md](consulta-freeform-scroll-blitter-en.md) | English consultation: best **lateral scroll** algorithm (min CPU + Blitter) for a **Free Form Sprite Layer** — world 40×16 px × 256 tall, viewport 20 columns, 8 reused sprite channels, copperlist emitted once; how to patch `SPRxCTL`/`SPRxPOS` and the incoming column DATA with the Blitter, frame split and copperlist strategy. |
