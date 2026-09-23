# HOST-271: descompresor ZX0 (A4) — vector de referencia

Verifica `eng::audio::zx0::decompress` (`eng/audio/zx0.hpp`) contra un flujo ZX0 **real generado
por el compresor de referencia de Einar Saukas** (`zx0`, https://github.com/einar-saukas/ZX0), no
por un codificador propio (eso solo probaría autoconsistencia).

## Qué comprueba

1. **Vector de referencia**: `kZX0[]` (22 bytes) es la compresión ZX0 de `kPCM[]` (32 bytes, rampa
   `0x80..0x8F` repetida). El flujo incluye un bloque de **literales** y un **match de nuevo
   offset** (offset 16, longitud 16, con el bit *interlazado* del offset/*backtrack*), y el
   decodificador reconstruye el PCM **byte a byte**.
2. **Dispatch** de `pcm_codec::decode`: `Codec::Zx0` (0) y `Codec::DeltaRle` (2).
3. **Límites**: destino pequeño → `-1`; `Codec::APLib` (aún no portado) → `-1`.

## Notas

- Port fiel de `dzx0.c` v2: bit-reader MSB-first, Elias gamma **interlazado**, `read_byte` que no
  toca el estado de bits y *backtrack* del bit bajo del LSB del offset.
- **aPLib** y la cabecera contenedora `AUZX` quedan como el resto de A4 (ver `ROADMAP_AUDIO.md`).

## Salida de referencia

```
OK: ZX0 (vector del compresor de referencia) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/audio/271_zx0
```
