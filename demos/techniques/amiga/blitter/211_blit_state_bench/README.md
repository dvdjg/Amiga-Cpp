# 211_blit_state_bench

Bench de **`FramePlan::sort_by_state()`** sobre una escena de blits **disjuntos** (tiles puros).

## Qué ilustra

El `FramePlan` permite agrupar sus `BlitJob` por **estado común del Blitter**
(`sort_by_state()`, ver `engine/include/eng/graphics/frame_plan.hpp`). Agrupar solo es lícito
si el orden no importa, es decir, si los jobs reordenados tienen **destinos disjuntos**. Una
rejilla de tiles de 16x16 que no se solapan es el consumidor mínimo que lo justifica.

El backend (`amiga_blitter.cpp`, `submit_blit_job`) mantiene una **caché de estado común**
(`BLTCON*`, ventanas y módulos) que omite sus ~8 escrituras a custom cuando un job comparte el
estado con el anterior. El contador `AmigaBackend::blitter_common_hits()` cuenta esas
reprogramaciones evitadas: es la magnitud que cuantifica `sort_by_state`.

## Escena

Rejilla de 16x7 tiles de 16x16 (112 jobs, dentro de `FramePlan::max_blit_jobs = 128`), cada uno
un `TileBlockCopy` a una celda disjunta. Los tiles usan uno de **dos estados** que difieren solo
en el **módulo de fuente** (`BLTCMOD`):

- estado A: fuente apretada (1 word por fila y plano), `source_modulo = 0`;
- estado B: fuente con word de guarda (2 words por fila), `source_modulo = 2`.

Con `K_BENCH_STATES=2` los estados se intercalan en **tablero de ajedrez**: en orden de emisión
cada job cambia de estado respecto al anterior. Al agrupar, los tiles de un mismo estado quedan
adyacentes. Los dos estados pintan colores distintos (verde/blanco) para que la rejilla sea
visible.

El resultado se publica en `g_eng_run_status.detail = (common_hits << 16) | blitter_starts`
(legible por el canal lateral del runner, campo `detail` del `run-report.json`).

## Resultado

Medido en A500 (OCS, debug), `--warp`, mismo bitmap en las dos variantes (capturas con el mismo
SHA-256):

| escena | `sort_by_state` | `blitter_starts` | `blitter_common_hits` |
|---|---|---|---|
| 2 estados intercalados | no | 448 | 6 |
| 2 estados intercalados | sí | 448 | 110 |
| 1 estado (control) | no | 448 | 112 |

`sort_by_state` no cambia el número de lanzamientos (`448 = 112 tiles × 4 planos`) ni el bitmap;
sube los aciertos de caché de 6 a 110 (≈832 escrituras a custom evitadas por frame). El control
homogéneo ya está saturado (112/112), de modo que agrupar es **neutro** cuando todos los blits
comparten estado: conviene activarlo solo donde ayuda (escenas heterogéneas con destinos
disjuntos).

Referencias: `docs/engine/architecture/RASTER.md` §"Prioridades de rendimiento (ROI)",
`docs/engine/architecture/BLITTER_INTENT_QUEUE.md` §4, `tests/host/graphics/387_frame_plan_state`.

## Comandos

```bash
# Sin agrupar (orden de emisión)
bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/211_blit_state_bench --debug

# Agrupado (activa sort_by_state)
EXTRA_DEFINES="-DK_BENCH_SORT=1" \
  bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/211_blit_state_bench --debug

# Control homogéneo (1 estado)
EXTRA_DEFINES="-DK_BENCH_STATES=1" \
  bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/211_blit_state_bench --debug

bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/211_blit_state_bench --warp
```
