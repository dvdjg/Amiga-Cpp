# HOST-272: contenedor AUZ2

Valida la cabecera portable del contenedor de audio por chunks que produce `tools/audio-compressor`. El payload Delta+RLE mantiene sus vectores específicos en HOST-242; ZX0, en HOST-271.

```bash
bash tools/run-host-tests.sh tests/host/audio/272_auz2
```
