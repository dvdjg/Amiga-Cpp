# HOST-451 — rachas de estado fijo del `FramePlan` (`BlobRun`)

Cubre la **clasificación de rachas homogéneas** que comparten las dos vías de ejecución del
`FramePlan` (`execute_frame_plan` síncrona y `execute_frame_plan_async` por IRQ):
`eng::graphics::blob_run_from` / `blob_run_same` (`blitter_state.hpp`) y su materialización con
`eng::amiga::BlobBatch` (estado fijo programado una vez; por objeto solo punteros, `shift` y
`BLTSIZE`).

El invariante que protege la optimización es la **equivalencia de registros**: una racha
clasificada debe escribir, por objeto, los mismos registros **efectivos** que el camino genérico
(`blitter_job_from`), con los canales que cada operación usa.

## Qué comprueba

- **Clasificación**: cookie-cut `$CA` / clear `$00` / OR `$FC` interleaved de un solo canal
  entran; descendente, planar multi-plano, `source_words_per_row ≠ 0`, minterm ajeno, `height`
  1024 y clear con salto entre planos quedan fuera.
- **`blob_run_same`**: dos objetos con el mismo estado fijo (aunque cambien punteros y `shift`)
  pertenecen a la misma racha; cualquier módulo distinto (o tipo distinto) la rompe;
  `MaskedBlobNoSave` comparte racha con `MaskedBobCookieCut`.
- **Equivalencia con el encoder** (`blitter_job_from`): `CON0/CON1` (con `ASH`/`BSH`),
  `AFWM/ALWM`, módulos de los canales usados, punteros (`A`=máscara/objeto, `B`/`C`/`D` según la
  operación) y `BLTSIZE` coinciden; un segundo objeto de la racha solo reescribe
  punteros/`shift`/`BLTSIZE`.
- **Intención real**: el `BlitJob` que produce `blit_job_from` para un `MaskedStamp` interleaved
  (el camino de `Screen::sprite`) clasifica como racha `CookieCut`.
- **Validación**: `blit_op_valid` (fuente/máscara obligatorias por tipo, rect no vacío, `y ≥ 0`,
  ancho ≤ 63 palabras, `ashift < 16`) y el rechazo del `FramePlan` para `words_per_row == 64` y
  `height > 1024` (no codificables en `BLTSIZE`).

## Ejecutar

```bash
bash ./tools/run-host-tests.sh tests/host/platform/amiga/451_blit_runs
```

## Referencias

- `engine/include/eng/graphics/blitter_state.hpp` (`BlobOp`, `BlobRun`, `blob_run_from`)
- `engine/include/eng/platform/amiga/blob_batch.hpp` (`BlobBatch`)
- `engine/include/eng/graphics/blit_queue.hpp` (`blit_op_valid`, `blitter_job_from`)
- `docs/engine/architecture/BLITTER_INTENT_QUEUE.md` §6 (rachas y vías de ejecución)
