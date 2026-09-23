# HOST-121: campos de bits (bitstream)

Test host de `engine/include/eng/core/util/bitstream.hpp`: `BitWriter`/`BitReader` sobre
un buffer de bytes del llamador, en orden **LSB-first**, con campos de 1..32 bits. Para
empaquetar niveles, assets y partidas guardadas donde cada byte cuenta.

## Qué comprueba

1. Round-trip de campos de 3, 16, 1 y 32 bits, y `bit_count`/`byte_count`.
2. Capacidad del buffer y `full` (el noveno bit no cabe en un byte).
3. Lectura más allá del final: falla sin consumir bits.
4. Anchos inválidos (0 y > 32).

## Salida de referencia

```
BitStream:
OK: BitStream (round-trip, capacidad, fin de buffer, anchos)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/121_bitstream
```
