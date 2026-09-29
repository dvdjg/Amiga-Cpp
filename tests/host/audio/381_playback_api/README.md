# HOST-381: API común de reproducción

Valida `eng/audio/playback.hpp`: una petición común para sample, stream o música devuelve un handle y permite expresar pausa, reanudación, parada y volumen sin exponer Paula, mixer ni punteros.

```bash
bash tools/run-host-tests.sh tests/host/audio/381_playback_api
```
