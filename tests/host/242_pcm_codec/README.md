# HOST-242: codec PCM Delta + RLE (`eng::audio::pcm_codec`)

Test host del codec de audio **Delta + RLE (ByteRun1)** (`engine/include/eng/audio/pcm_codec.hpp`),
freestanding y sin heap, usado por el streaming digital desde disquete
(`docs/engine/architecture/AUDIO_STREAMING.md`).

## Que comprueba

1. **Round-trip byte a byte** (`encode` → `decode`) sobre silencio, rampa, cuadrada y ruido
   pseudoaleatorio, en tamanos 1/2/3/128/129/256/1024/4096.
2. **Ratio**: el silencio comprime a ≤64 bytes (deltas 0 → RLE); el ruido no se expande sin control.
3. **Rechazos**: codec desconocido (`compression != 2`), destino demasiado pequeno y flujo truncado
   devuelven `-1`.
4. **Sin desbordar**: `encode` con destino minusculo devuelve `-1` en vez de escribir fuera.

## Salida de referencia

```
OK: codec PCM Delta+RLE (round-trip y rechazos) validado.
```

## Ejecutar

```bash
bash tools/run-host-tests.sh tests/host/242_pcm_codec
```
