# HOST-136: escalares generales en simulacion (`eng::real`/`coord`/`intw`)

Test host que escribe un algoritmo de simulacion **una sola vez** contra los alias de
`eng/core/scalar.hpp` (`eng::real`, `eng::coord`, `eng::intw`) y lo compila en los tres modos
del escalar. Es la evidencia de la fase **F5** del
[roadmap del escalar genérico](../../../docs/guides/roadmap/REFACTOR_SCALAR_GENERICO.md): el
código no cambia entre anchos, solo la selección en compilación.

## Qué comprueba

1. **Oscilador semi-implícito** integrado 200 pasos, escrito solo con `eng::real`; el error
   frente a la misma recurrencia en `double` debe quedar bajo la tolerancia del modo.
2. **`eng::coord`** (coordenada de simulación) con suma y comparación, válidas en los tres
   modos.
3. **`eng::intw`** (entero de palabra natural) con `min`/`max`/`clamp` exactos.

Modos y escalares:

| Macro | `eng::real` | `eng::coord` | `eng::intw` |
|---|---|---|---|
| (ninguna, host) | `float` | `int` | `int` |
| `ENG_SCALAR_RETRO16` | `Fixed<s16,12>` | `Fixed<s16,0>` | `s16` |
| `ENG_SCALAR_RETRO32` | `Fixed<s32,12>` | `Fixed<s32,0>` | `s32` |

## Ejecutar

```bash
# Un modo (nativo en host), dentro del runner normal:
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/136_real_scalar

# Los tres modos, comparados en la misma pasada:
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run/run-scalar-modes.sh
```

## Salida de referencia (los tres modos)

```
== native ==
RealScalar: mode=native  sizeof(real)=4 sizeof(coord)=4
  oscilador: x=-0.897429 ref=-0.897429 err=2.13e-08 (tol 1e-04)
OK: eng::real/coord/intw (modo native) validados

== RETRO16 ==
RealScalar: mode=retro16  sizeof(real)=2 sizeof(coord)=2
  oscilador: x=-0.879150 ref=-0.897429 err=2.04e-02 (tol 5e-02)
OK: eng::real/coord/intw (modo retro16) validados

== RETRO32 ==
RealScalar: mode=retro32  sizeof(real)=4 sizeof(coord)=4
  oscilador: x=-0.879150 ref=-0.897429 err=2.04e-02 (tol 5e-02)
OK: eng::real/coord/intw (modo retro32) validados
```

RETRO16 y RETRO32 dan el mismo resultado porque `eng::real` es `Fixed<s16,12>` y
`Fixed<s32,12>`: comparten exponente `E=12` (resolución `1/4096`). La anchura de la
representación da **rango**, no precisión fraccionaria; para esta última habría que subir `E`.
