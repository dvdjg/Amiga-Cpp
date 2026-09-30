# 212_blitter_feeder

Bench de una cadena de copias `CopyRect` interleaved programada por polling frente a un *feeder* de IRQ de Blitter.

## Qué ilustra

La demo copia las 20 columnas de 16 píxeles de un bitmap planar interleaved de 320x256 y 3 planos. Cada columna usa un único `BlitJob`: su altura es `256 * 3` planelines y sus módulos hacen que el Blitter salte de un plano al siguiente.

- `K_FEEDER_MODE=0` (poll): la CPU programa cada columna y espera a que termine antes de programar la siguiente.
- `K_FEEDER_MODE=1` (IRQ): la CPU programa solo la primera columna; la IRQ de fin de Blitter programa las siguientes sin bloquear el bucle principal.

El origen es una imagen estática de bandas de color. La copia es idempotente para que una captura durante la cadena no introduzca *tearing* visible y para poder comparar las dos variantes por SHA-256. La demo comprueba una vez que `dst == src` y publica las métricas en `g_eng_run_status.detail`.

## `detail`

```text
bit 31       verified: la copia completa dejó dst igual a src
bits 24..30 blitter_common_hits por frame
bits 18..23 blitter_starts por frame
bits 0..17   ciclos de CPU por frame
```

Los promedios dependen del momento exacto de la captura. En una ejecución A500 debug con `--warp` se observó:

| modo | verificado | starts/frame | common hits/frame | ciclos CPU/frame |
|---|---:|---:|---:|---:|
| poll | 1 | 20 | 20 | 178602 |
| IRQ | 1 | 19 | 19 | 2407 |

El valor IRQ puede aparecer como 19 en vez de 20 porque el promedio se toma en un límite de frame mientras la cadena continúa entre frames. La evidencia visual fue idéntica en ambas variantes: SHA-256 `922D1BD083BA941B1DC295B14C7869EDE0FE8CF0B488FCE6E49CCA739469CFA4`.

## Comandos

```bash
# Poll
bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/212_blitter_feeder --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/212_blitter_feeder --warp

# Feeder IRQ
EXTRA_DEFINES="-DK_FEEDER_MODE=1" \
  bash ./tools/build/build-demo.sh demos/techniques/amiga/blitter/212_blitter_feeder --debug
bash ./tools/run/run-demo.sh demos/techniques/amiga/blitter/212_blitter_feeder \
  --config A500_k_feeder_mode1_debug --warp
```

Referencias: [`BLITTER_INTENT_QUEUE.md`](../../../../../docs/engine/architecture/BLITTER_INTENT_QUEUE.md) §6 y [`blitter-memcpy.md`](../../../../../docs/reference/amiga/techniques/blitter-memcpy.md) §3.
