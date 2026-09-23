# HOST-111: blackboard de IA

Test host de `engine/include/eng/ai/decision/blackboard.hpp`:
`eng::ai::Blackboard<Key, Value, MaxKeys>`, la memoria compartida de la IA. Un `enum`
denso de claves (`[0, MaxKeys)`) indexa un array de valores y un `BitSet` de presencia:
`find` es `O(1)` y no hay heap. Para difundir cambios se usa `eng::util::Event` en
paralelo (el blackboard es almacenamiento puro).

## Qué comprueba

1. `set`/`find`/`contains`/`get_or`/`erase`/`clear` y `size`.
2. Sobrescritura de una creencia (no duplica).
3. Valores de tipo `struct` del juego (no solo escalares).

## Salida de referencia

```
Blackboard:
OK: Blackboard (set/find/get_or/erase/clear, structs)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/111_blackboard
```
