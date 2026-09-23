# HOST-123: consumidor de bitstream y dynamic_bitset

Test host que usa `bitstream.hpp` y `dynamic_bitset.hpp` para un caso real (no prueba las
APIs en aislamiento; eso es HOST-121/122):

1. **Nivel empaquetado** con `BitWriter`/`BitReader`: cabecera (6 bits ancho + 6 alto) y,
   por celda, id de tile de 4 bits + flag de sólido de 1 bit. Se empaqueta y se lee de
   vuelta comprobando el **round-trip 100 %** y el tamaño (42 B frente a 128 B del formato
   ingenuo de un byte por celda).
2. **Set de tiles sucios** de un mapa de 4096 tiles con `DynamicBitSet` en una arena de
   512 B: se marcan solo las celdas cambiadas, se cuentan y se limpia una región.

## Salida de referencia

```
BitsConsumer:
OK: consumidores (nivel empaquetado y set de tiles sucios)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/123_packed_level
```
