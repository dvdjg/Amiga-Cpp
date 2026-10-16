# Blitter ocupado: escrituras de registros y `BLTSIZE` en curso

Qué ocurre cuando la CPU escribe registros del Blitter (en particular `BLTSIZE`) **mientras hay un
blit en ejecución**. Fija el contrato que sigue el motor (el plan espera por job) y la evidencia en
la fuente de WinUAE.

## Mecanismo observado

| Aspecto | Fuente |
|---|---|
| Contrato del manual | AHRM 3.ª, cap. 6 «Multitasking and the Blitter»: «When a blit is in progress, **none of the blitter registers should be written**.» El `DMAF_BLT DONE` (BBUSY) solo es fiable tras leer un registro de Chip (los chips antiguos lo ponen al primer ciclo DMA, no al escribir `BLTSIZE`). |
| Escritura de `BLTSIZE` | `custom.cpp:3982` (`BLTSIZE_func`) → `do_blitter(...)`; con `cpu_cycle_exact` la escritura pasa por el pipeline (`custom.cpp:4017`, `pipelined_custom_write` 1 CCK). |
| Con blit activo | `blitter.cpp:2036` avisa `blitter was already active!` (solo log), `blitter.cpp:2058` re-ejecuta `blitter_start_init()` (re-captura TODO el estado, `blitter.cpp:1097+`) y `blitter.cpp:2101`/`:2119` reinician los contadores (`blitter_hcounter/vcounter = 0`): el blit en curso **se pierde/reinicia con los parámetros nuevos**. |
| «Pending» | `blt_info.blit_pending` solo cubre «DMA apagado al escribir `BLTSIZE`» (`blitter.cpp:2155` `blitter_check_start`); no es una cola para escrituras con el blitter activo. |

## Contraste con el AHRM

El manual no describe ninguna cola de `BLTSIZE`; la práctica de la época es `WaitBlit()` antes de
reprogramar. El motor lo aplica ahora: **cada job del plan espera a `BBUSY==0`** antes de escribir
sus registros (`amiga_blitter.cpp`, `submit_blit_job`; las rachas `BlobBatch` ya esperan por objeto
en `one()`). En la vía async la espera es de coste ~0: al entrar en la IRQ de fin de blit el Blitter
ya está libre.

## Implicación para el engine

- Encadenar jobs **sin** esperar solo «funcionaba» cuando cada blit terminaba antes de programar el
  siguiente (dependiente del bus); con un blit largo delante, un burst de jobs cortos pierde planos
  (caso real: DT-022, demo 121: restaurador `CopyRect` + actor cookie-cut en un plan → plano 0
  perdido de forma intermitente y posicional).
- El coste medido del contrato (espera por job) en la 213 es **nulo dentro del ruido** (sync
  32,30 vs 32,20–32,32 fps; async 27,41 vs 27,58), porque el frame está limitado por el Blitter.

## Enlaces

- Código: `engine/src/platform/amiga/amiga_blitter.cpp` (`submit_blit_job`), `blob_batch.hpp`
  (`one()`), `docs/engine/architecture/BLITTER_INTENT_QUEUE.md` §6.
- Investigación: `docs/debugging/investigaciones/121-actor-plano-0-y-carencias-blits.md`
  (síntoma, descartes y mitigación en la demo).
- Deuda: `ROADMAP_DEUDA_TECNICA.md` DT-022 (motor) y DT-023 (capturas de secuencia a mitad de
  frame, que contaminaron el diagnóstico).
