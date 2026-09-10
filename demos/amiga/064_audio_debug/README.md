# Demo 064: audio debug — onda senoidal pura en un canal

Primer eslabón del procedimiento de depuración de sonido
(`docs/debugging/AUDIO_DEBUG.md`): reproduce un ciclo de seno (64 muestras, ±127)
en bucle por AUD0, sin mixer ni música, y expone el estado real del hardware.

```bash
tools/build/build-demo.sh demos/amiga/064_audio_debug --clean
tools/run/run-demo.sh       demos/amiga/064_audio_debug
```

Verificación: `run-report.json` -> `detail` codifica `DMACONR` (bits altos) y
`sample_max`/`sample_min` (bits bajos). Debe dar `DMACONR=0x381` (`AUD0EN`
activo), `max=127`, `min=-127`. El diagnóstico completo está en `g_audio_dbg`
(leíble por canal lateral).
