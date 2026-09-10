# Demo 057: audio mixer — tono de Paula vía AudioMixer → AudioPlan → Paula

Demuestra el paso 7 de `ENGINE_DESIGN.md` §5: la cadena completa de audio
`SampleEvent` → `eng::audio::AudioMixer` (lógica pura) → `AudioPlan` →
`eng::amiga::PaulaAudio` (registros de Paula + DMA).

Qué suena: un tono cuadrado en bucle por el canal 0. Arriba/abajo cambia el tono
(período de `AUD0PER`); FIRE silencia (volumen 0). Visualmente, un degradado de
fondo con una barra cian cuya altura refleja el tono.

## Verificación

- Test host `tests/host/005_audio`: valida la lógica de asignación de canales del
  `AudioMixer` (first-fit, overflow, hint).
- `mark_ready` guarda en `detail` el valor leído de `DMACONR` tras arrancar el
  audio; el `run-report.json` debe mostrar `AUD0EN` (bit 0) y `DMAEN` (bit 9)
  activos (p. ej. `0x381` = master+copper+bitplane+AUD0).

```bash
CXX="<g++ nativo>" tools/run-host-tests.sh tests/host/005_audio
tools/build/build-demo.sh demos/amiga/057_audio_mixer --clean
tools/run/run-demo.sh       demos/amiga/057_audio_mixer
```

Nota: los registros `AUDxLEN/PER/VOL` son de solo escritura; la evidencia de
arranque es `DMACONR` (DMA de audio activo) y que la demo llega a READY sin
volcar.
