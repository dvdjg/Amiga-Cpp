# `audio-compressor`

Empaquetador offline para producir archivos `AUZ2`, el contenedor de audio PCM por chunks que puede leer el engine Amiga. Es un binario de PC independiente, pero incluye `engine/include` para reutilizar el contrato del formato y `eng::audio::pcm_codec`; no duplica los algoritmos de Delta+RLE ni ZX0.

## Compilación

Desde la raíz del repositorio:

```bash
bash host-tools/audio-compressor/build.sh
```

El binario se genera en `out/tmp/audio-compressor/audio-compressor`. Se puede seleccionar otro compilador con `CXX=g++`.

## Uso

```bash
out/tmp/audio-compressor/audio-compressor pack input.wav out.auz2 --codec delta-rle --chunk 1024 --verify
out/tmp/audio-compressor/audio-compressor pack input.raw out.auz2 --raw-rate 11025 --codec raw
```

La versión inicial acepta WAV PCM mono de 8 o 16 bits y RAW PCM8 mono con signo. El formato se limita deliberadamente a mono PCM8 para coincidir con Paula. `--verify` descomprime todos los chunks y compara cada byte con la onda normalizada de entrada.

Las salidas son generadas y deben permanecer bajo `out/`; el programa no escribe junto al código fuente.
