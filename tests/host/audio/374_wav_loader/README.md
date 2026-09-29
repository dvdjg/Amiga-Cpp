# HOST-374: ingestión WAV

Valida el loader host-only de `host-tools/pack-pcm`: WAV PCM lineal mono/estéreo de 8/16 bits, downmix estéreo, conversión a PCM8 con signo y override de frecuencia.

```bash
bash tools/run-host-tests.sh tests/host/audio/374_wav_loader
```
