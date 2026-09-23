# HOST-137: primitivas de concurrencia (`eng::parallel`)

Test host de `engine/include/eng/parallel/parallel.hpp`: la **frontera de
concurrencia** del engine, que compila igual en un Amiga de una sola CPU (no-ops,
`hardware_threads()==1`) que en una máquina moderna con hilos reales
(`std::thread`/`std::mutex`/`std::atomic`/`std::condition_variable`).

## Qué comprueba

1. `hardware_threads()` (≥1), `has_threads()` coherente.
2. `Mutex` + `LockGuard` (cierre RAII) y `Atomic<u32>` (load/store/`fetch_add`/
   `exchange`/`compare_exchange`).
3. `Thread`: 4 hilos incrementan un atómico 25 000 veces cada uno y el total es
   exacto tras `join`.
4. `ConditionVariable`: un worker espera hasta que el hilo principal fija el
   predicado y notifica.
5. `StopSource`/`StopToken`: la cancelación es visible al token.
6. `for_each_index`: reparte todas las iteraciones exactamente una vez.

## Salida de referencia

```
eng::parallel:
OK: eng::parallel (hilos, mutex, atomicos, cv, stop, for_each_index)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/137_parallel
```
