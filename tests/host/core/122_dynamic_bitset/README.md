# HOST-122: bitset de tamaño elegido en `init`

Test host de `engine/include/eng/core/util/dynamic_bitset.hpp`:
`eng::util::DynamicBitSet<A>`, conjunto de bits cuyo tamaño se fija en `init` y que
reserva sus palabras en un `Allocator` (sin heap). Complementa a `BitSet<N>` (fijo).

## Qué comprueba

1. `init` reserva y deja a cero; `set`/`test`/`flip`/`reset`/`count`/`any`/`clear`.
2. Capacidad insuficiente → `init` devuelve `false`.
3. `init(0)` es válido y vacío; la máscara de la última palabra parcial no cuenta de más.

## Salida de referencia

```
DynamicBitSet:
OK: DynamicBitSet (init, set/test/count, capacidad, palabra parcial)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/122_dynamic_bitset
```
