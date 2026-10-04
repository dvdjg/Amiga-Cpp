# `pack-pcm` — empaquetador de PCM a AUZX (PC)

`pack-pcm` es el empaquetador de bajo nivel. Para arrastrar un WAV, clasificar automáticamente samples/música y generar AUZX/ACP1 se usará la utilidad orquestadora `host-tools/audio-compressor/audio-compressor`, descrita en [`docs/engine/architecture/AUDIO_COMPRESSION.md`](../../docs/engine/architecture/AUDIO_COMPRESSION.md).

Herramienta de PC que empaqueta PCM mono 8-bit con signo en un contenedor **AUZX**
(`engine/include/eng/audio/auzx.hpp`) usando los **mismos codificadores del engine**
(`eng::audio::pcm_codec::encode`), de modo que el fichero es compatible con lo que consume el
Amiga sin deriva de formato.

## Uso

```
pack-pcm <in.raw|in.wav> <out.auzx> [none|rle|fib|ima] [sample_rate] [chunk_samples]
```

- `in.raw`: PCM mono 8-bit con signo (1 byte/muestra). La frecuencia por defecto es 8000 Hz.
- `in.wav`: WAV PCM lineal mono o estéreo, de 8 o 16 bits. El programa hace downmix estéreo y
  normaliza a PCM8 con signo. La frecuencia del WAV se conserva salvo que se indique
  `[sample_rate]`; en ese caso **se remuestrea** (interpolación lineal, `resample_stems`), de modo
  que cambia el número de muestras y se conserva la duración temporal, no solo la etiqueta de tasa.
- `codec`: `none` (crudo), `rle` (Delta+RLE, por defecto), `fib` (Fibonacci Delta / 8SVX),
  `ima` (IMA ADPCM).
- `sample_rate`: frecuencia explícita opcional; si se omite en WAV se usa la frecuencia del WAV y en
  RAW se usa 8000 Hz. Indicarla en WAV dispara el remuestreo lineal a esa tasa (ver arriba).
- `chunk_samples`: potencia de 2 (por defecto 4096). El PCM se **rellena** al final hasta un
  múltiplo de `chunk_samples`, para que todos los chunks descompriman a `chunk_samples` (contrato
  de `PcmStream`); `total_samples` incluye ese relleno.

Para **Delta+ZX0** / **ZX0**: aplicar el paso delta y comprimir con la herramienta de
referencia `zx0 -f` (ver `docs/engine/architecture/AUDIO_STREAMING.md` §7.1); el motor AUZX
es el mismo.

## Compilar

```bash
g++ -std=gnu++23 -Iengine/include host-tools/pack-pcm/pack-pcm.cpp -o pack-pcm
```

## Verificación

Tras escribir, el programa **relee** el fichero, lo parsea, decodifica con `pcm_codec::decode`
y comprueba el round-trip: informa `exacto` (codecs sin pérdida) o `con perdida`
(`fib`/`ima`). Devuelve 0 si el contenedor y el codec son coherentes.
