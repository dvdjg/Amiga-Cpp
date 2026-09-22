# Investigaciones y hallazgos

Hallazgos concretos de depuración: **bloqueos abiertos**, **post-mortems/lecciones** y **consultas a IA externa**. Son bitácoras (narran el proceso, lo descartado y las hipótesis); el estado vigente del engine está en `docs/engine/` y las rarezas del emulador en `docs/reference/emulators/`.

## Abiertas (bloqueos vigentes)

| Documento | Descripción |
|-----------|-------------|
| [octamed-startmusic-hang.md](octamed-startmusic-hang.md) | **A1**: el playroutine OctaMED (KONEY) arranca con `jsr _startmusic` pero **cuelga** bajo el engine. Infra validada (VASM + `INCBIN`, módulo MMD1); descartados VBlank/CIA, sin mixer y `a6`. Hipótesis y pasos (GDB). |
| [audio-stream-irq-rate.md](audio-stream-irq-rate.md) | **A5**: en `272_audio_stream` la IRQ de audio (nivel 4) dispara ~34× más rápido que `AUDxPER * AUDxLEN` (~517/s vs ~15/s) → *underruns* y comportamiento errático. Incluye el mecanismo (AHRM `:4378`; WinUAE `audio.cpp:2523/2631/2722`), lo descartado (`ADKCON`, INTREQ de otro canal, `period_for_hz`, registros write-only) y dos bugs reales corregidos de paso (estado compartido con la IRQ sin `volatile`; contadores de 32 bits que se desgarran). |
| [captura-negra-intermitente-050-051.md](captura-negra-intermitente-050-051.md) | Las demos 050/051 producen a veces una captura 100 % negra pese a READY. No es el fuente (051↔050), ni el config (ruta `dh1`), ni los blits; es intermitente/por estado. Siguiente paso: leer registros reales por GDB. |
| [scene-rebuild-efectos.md](scene-rebuild-efectos.md) | Migrar una demo al modelo de efectos con **reconstrucción por frame** del `Scene` descoloca la copperlist (la demo validada se revirtió). Hipótesis y cómo atacarlo (test host comparativo). |
| [winuae-pantalla-negra-arranque.md](winuae-pantalla-negra-arranque.md) | Pantalla negra al arrancar WinUAE (el sistema no botea). |
| [diagnostico-adf-negro.md](diagnostico-adf-negro.md) | El ADF se queda en negro (diagnóstico). |
| [diagnostico-depurador-f5.md](diagnostico-depurador-f5.md) | El depurador no se lanza con F5 (diagnóstico). |
| [pending-verification.md](pending-verification.md) | Repaso pendiente de generalidad de interfaces y modelado del engine. |

## Hallazgos y lecciones (cerrados)

| Documento | Descripción |
|-----------|-------------|
| [debug-demo-arranque-doble-texto-banda.md](debug-demo-arranque-doble-texto-banda.md) | **Resuelto**: artefactos de arranque de 060/201 — doble texto (`draw_text` sin corte en NUL), banda cian 0x0AA (sprite DMA del sistema vivo, causalidad A/B), copperlist fuera de VBL (COPJMP1) y paleta del pie. Fixes, sondas y evidencia (§11). |
| [lecciones-porte-blitter-demoscene.md](lecciones-porte-blitter-demoscene.md) | Post-mortem del Blitter de `flatshade-convex`: la raya por vértice, por qué se «normalizó» un truco de registro (`BLTDPTR`), el papel del AHRM y el checklist al importar efectos. |
| [lecciones-engine-cpp23.md](lecciones-engine-cpp23.md) | Lecciones del engine C++23: tests que escondían bugs, `-Werror=narrowing`, medir A/B, no duplicar `constexpr`, `runtime_palette()`/`apply_into`, numeración y separar referencia de bitácora. |
| [audio-debug.md](audio-debug.md) | Procedimiento de depuración de sonido: analizar la onda en host (generador/analizador), puente a C++ y verificación por canal lateral (DMACONR, registros AUDx, volcado). |
| [106_sesion-tilefield.md](106_sesion-tilefield.md) | Sesión de desarrollo: API `TileField` + demo 106 (anillo de tres tramos). |
| [112_bg-flicker.md](112_bg-flicker.md) | Demo 112: flicker de 1 px del fondo RoboCod (bitmap único); análisis y decisión. |
| [board-selfplay-and-perf.md](board-selfplay-and-perf.md) | Board games: coherencia en host y rendimiento en Amiga. |
| [npc-table-scenarios.md](npc-table-scenarios.md) | Laboratorio de escenarios de mesa (`eng::sim` + `eng::cards`). |
| [sim-ecosystem-scenarios.md](sim-ecosystem-scenarios.md) | Laboratorio de escenarios del ecosistema (`eng::sim`). |

## Consultas a IA externa (autocontenidas)

| Documento | Descripción |
|-----------|-------------|
| [consulta-grok-disco-y-loader.md](consulta-grok-disco-y-loader.md) | Disco a bajo nivel (`df0:` sin Workbench, `trackdisk.device`, buffers DMA en Chip), carga `.englib`+HUNK, teclado, E/S async y ADF datos/arranque. Estado verificado, evidencia y preguntas. |
| [consulta-optimizacion-blitter-demoscene.md](consulta-optimizacion-blitter-demoscene.md) | El port C++ de `flatshade-convex` es 2.4× más lento que el original (670k vs 287k ciclos/frame) pese a los mismos blits; desglose por secciones y preguntas. |
| [consulta-asm-flatshade.md](consulta-asm-flatshade.md) | Consulta: port ASM m68k de `flatshade-convex` (demo 116). |
| [consulta-asm-flatshade-seguimiento.md](consulta-asm-flatshade-seguimiento.md) | Seguimiento 1: aplicados los fixes, sigue negro. |
| [consulta-asm-flatshade-seguimiento2.md](consulta-asm-flatshade-seguimiento2.md) | Seguimiento 2 de la consulta ASM de `flatshade-convex`. |
| [consulta-asm-flatshade-seguimiento3.md](consulta-asm-flatshade-seguimiento3.md) | Seguimiento 3 de la consulta ASM de `flatshade-convex`. |
| [consulta-asm-flatshade-seguimiento4.md](consulta-asm-flatshade-seguimiento4.md) | Seguimiento 4 de la consulta ASM de `flatshade-convex`. |
