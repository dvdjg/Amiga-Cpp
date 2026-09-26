# HOST-357: Fibonacci Delta (IFF 8SVX)

Test host de `engine/include/eng/audio/fib_delta.hpp`: el códec de audio **con pérdida** del
estándar IFF 8SVX (`sCompression = 1`), 4 bits por muestra (2:1 constante). La decodificación
es un nibble, una lectura de tabla de 16 bytes y un `ADD.B` por muestra — apto para que Paula
reproduzca por DMA mientras la CPU decodifica.

## Qué comprueba

1. **Vector dorado** calculado a mano desde el Apéndice C: `[pad=0][x0=0][0x01][0x8F]` →
   muestras `-34, -55, -55, -34`.
2. **Equivalencia con el estándar**: el decodificador del engine coincide byte a byte con una
   reimplementación independiente del `DUnpack`/`D1Unpack` de la especificación sobre un flujo
   pseudoaleatorio.
3. **Encoder**: si los deltas son de la tabla, `decode(encode(x)) == x` (sin pérdida en ese
   caso); el `encode` de Delta+RLE/ZX0 sigue su camino.
4. **Dispatch** `Codec::FibDelta` y errores de tamaño (flujo corto, destino pequeño).

## Salida de referencia

```
OK: Fibonacci Delta (IFF 8SVX) validado contra el estandar.
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/audio/357_fib_delta
```
