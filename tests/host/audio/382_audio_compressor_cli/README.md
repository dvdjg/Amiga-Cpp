# HOST-382: CLI única audio-compressor

Valida la CLI de la aplicación única: crea un WAV estéreo sintético, comprueba `--mode sample`→AUZX, remuestreo real con `--sample-rate`, rechazo de codec desconocido, `--mode music`→ACP1 v2 con eventos secuenciales/round-trip, `--hpss` con cuatro pistas y una fuente FLAC multicanal decodificada por FFmpeg.

El binario se compila aparte (nombre por plataforma; en POSIX sin `.exe`):

```bash
mkdir -p out/tmp/audio-compressor
# Linux/host:
g++ -std=gnu++23 -O2 -Iengine/include -Ihost-tools/pack-pcm host-tools/audio-compressor/src/main.cpp -o out/tmp/audio-compressor/audio-compressor
# Windows:
g++ -std=gnu++23 -O2 -Iengine/include -Ihost-tools/pack-pcm host-tools/audio-compressor/src/main.cpp -o out/tmp/audio-compressor/audio-compressor.exe
bash tools/run-host-tests.sh tests/host/audio/382_audio_compressor_cli
```

Si el binario está en otra ruta, usar `AUDIO_COMPRESSOR_BIN=<ruta>` para que el test lo invoque.

**Sin binario el test se OMITE** (`exit 3`, convención del runner: test host-only cuya dependencia
externa falta). El runner lo muestra como `SKIP` y **no** rompe la suite. El comando se adapta a la
plataforma (en POSIX no usa `cmd`).
