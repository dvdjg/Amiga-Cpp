# HOST-382: CLI única audio-compressor

Valida la primera vertical de la aplicación única: crea un WAV estéreo sintético, ejecuta el binario host con `--mode sample` y comprueba que produce un contenedor AUZX.

El binario debe compilarse antes en `out/tmp/audio-compressor/audio-compressor.exe`:

```bash
mkdir -p out/tmp/audio-compressor
g++ -std=gnu++23 -O2 -Iengine/include -Ihost-tools/pack-pcm host-tools/audio-compressor/src/main.cpp -o out/tmp/audio-compressor/audio-compressor.exe
bash tools/run-host-tests.sh tests/host/audio/382_audio_compressor_cli
```

Si el binario está en otra ruta, usar `AUDIO_COMPRESSOR_BIN=<ruta>` para que el test lo invoque.
