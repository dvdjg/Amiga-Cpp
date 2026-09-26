# HOST-359: IMA ADPCM 4-bit

Test host de `engine/include/eng/audio/ima_adpcm.hpp`: códec de audio **con pérdida** (tablas
estándar IMA/DVI, predictor de 16 bits + índice de paso adaptativo, 4 bits por muestra).

## Qué comprueba

1. **Referencia independiente**: el decodificador del engine coincide byte a byte con una
   reimplementación del estándar IMA escrita aparte en el test.
2. **Round-trip acotado**: `decode(encode(x))` sobre una señal suave tiene error máximo ≤ 8 y la
   primera muestra es exacta; comprime a ~4 bits/muestra.
3. **Dispatch** `Codec::ImaAdpcm` y rechazos de tamaño (bloque corto, destino pequeño).

## Salida de referencia

```
OK: IMA ADPCM (referencia independiente + round-trip acotado).
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/audio/359_ima_adpcm
```
