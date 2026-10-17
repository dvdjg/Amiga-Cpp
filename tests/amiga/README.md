# Tests on-target (`tests/amiga/`)

Tests de integración que corren **en el Amiga emulado** (WinUAE) y se validan por el canal lateral (`g_eng_run_status`), porque ejercitan hardware o ASM que el host no reproduce.

## Estructura

- `l0_bare_metal/`: sin SO ni servicios del engine (display básico, escalares).
- `l1_backend/`: con `AmigaBackend`/`App` (memoria, display, audio).

Cada test es una app del flujo normal (`tools/build/build-demo.sh`) con su `verify.sh`, que compila, arranca en el emulador, espera READY y **comprueba el `detail`** del run-status (bits de checks + evidencia).

## Índice

| Test | Qué valida | Evidencia (`detail`) |
|---|---|---|
| `l1_backend/001_app_display_teardown` (L1-001) | `App` compone display, materializa mundo y **detiene DMA** antes de liberar la escena; el pool Chip queda devuelto | 5 bits: DMA apagado, Chip recuperado, App arrancado, fondo materializado y añadido |
| `l1_backend/002_audio_leases` (L1-002) | Con el **mixer ASM real**: 4 canales software, rechazo por prioridad menor, reocupación tras `stop_sfx` y `AUD0EN`/DMA apagado tras `shutdown` | 6 bits + `DMACONR` leído con SFX activo en la mitad alta |

## Ejecución

```bash
bash tests/amiga/l1_backend/001_app_display_teardown/verify.sh
bash tests/amiga/l1_backend/002_audio_leases/verify.sh
```

`--skip-build` reutiliza el binario ya compilado. Los puertos del emulador son los de `docs/debugging/system/debug-winuae-v2-guide.md` (`WINUAE_GDB_PORT`/`WINUAE_SIDE_CHANNEL_PORT`).

Referencias: `docs/testing/README.md` (taxonomía y IDs), `docs/build/BUILD_AND_RUN.md` (runner), `ROADMAP_API_COHERENCE.md` §F8 (origen de los canarios de audio) y `ROADMAP_DEUDA_TECNICA.md` DT-029 (arranque de la 011).
