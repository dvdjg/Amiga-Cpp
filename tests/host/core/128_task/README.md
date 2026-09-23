# HOST-128: tareas secuenciales *stackless*

Test host de `engine/include/eng/core/util/task.hpp`: el patrón «`switch` + estado en un
`struct`» para scripting/secuencias, sin corrutinas de C++20 ni pila propia.

## Qué comprueba

1. `TaskSequence<N>` encadena pasos instantáneos en un mismo tick hasta terminar.
2. Un paso que devuelve `Running` **espera** al tick siguiente.
3. `Failure` **aborta**: los pasos siguientes no se ejecutan.
4. `Delay` espera N ticks y luego da `Success`.

## Salida de referencia

```
Task:
OK: Task (secuencia, espera, fallo, Delay)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/core/128_task
```
