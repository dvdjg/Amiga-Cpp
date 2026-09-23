# HOST-150: lectura/escritura binaria segura

Test host de `engine/include/eng/core/util/binary.hpp`: `ByteReader`/`ByteWriter`,
cursores little-endian sobre `Span` con comprobación de límites.

## Qué enseña / comprueba

- **Round-trip**: `write_u8/u16/u32/s16/s32` y sus `read_*` devuelven exactamente lo
  escrito, little-endian, con seguimiento de posición y bytes restantes.
- **Límites**: un `write`/`read` que no cabe devuelve `false` y **no avanza** el
  cursor (nada de desbordamientos silenciosos).
- **Bloques**: `read_into`, `read_view` y `write_bytes` copian tramos de `Span` sin
  `memcpy` ni aritmética de punteros.
- Sustituye a `*ptr++`/`reinterpret_cast`, que en 68000 fallan por alineación.

## Salida de referencia

```
eng::util::binary:
OK: binary (round-trip, little-endian, limites y copias)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/core/150_binary
```
