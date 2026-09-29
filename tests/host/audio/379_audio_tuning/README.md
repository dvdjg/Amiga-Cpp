# HOST-379: parámetros de audio genéricos

Valida `eng/audio/audio_tuning.hpp` con `float` y `Fixed<s32,16>`. El algoritmo no incluye un escalar concreto y usa las operaciones normalizadas del core.

```bash
bash tools/run-host-tests.sh tests/host/audio/379_audio_tuning
```
