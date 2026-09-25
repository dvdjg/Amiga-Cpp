# `pack-pcm` — empaquetador de PCM a AUZX (PC)

Herramienta de PC que empaqueta PCM mono 8-bit con signo en un contenedor **AUZX**
(`engine/include/eng/audio/auzx.hpp`) usando los **mismos codificadores del engine**
(`eng::audio::pcm_codec::encode`), de modo que el fichero es compatible con lo que consume el
Amiga sin deriva de formato.

## Uso

```
pack-pcm <in.raw> <out.auzx> [none|rle|fib|ima] [sample_rate] [chunk_samples]
```

- `in.raw`: PCM mono 8-bit con signo (1 byte/muestra). Se genera con
  `tools/audio/prep-sample.ts` (WAV → PCM) o cualquier conversor.
- `codec`: `none` (crudo), `rle` (Delta+RLE, por defecto), `fib` (Fibonacci Delta / 8SVX),
  `ima` (IMA ADPCM).
- `sample_rate`: 8000 / 11025 / 16000 / 22050 (por defecto 8000).
- `chunk_samples`: potencia de 2 (por defecto 4096).

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
