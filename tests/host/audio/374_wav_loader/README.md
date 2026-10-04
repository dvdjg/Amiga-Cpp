# HOST-374: ingestión WAV

Valida el loader host-only de `host-tools/pack-pcm`: WAV PCM lineal mono/estéreo de 8/16 bits, preservación ordenada de canales WAV de hasta ocho stems, downmix mono, conversión a PCM8 con signo y **override de frecuencia con remuestreo lineal** (`resample_stems`: la salida conserva la duración y cambia el número de muestras, no solo la etiqueta de tasa). Los casos cubren el WAV8 de tres canales con y sin override, y el WAV16 mono con y sin override.

```bash
bash tools/run-host-tests.sh tests/host/audio/374_wav_loader
```
