# Tests HOST — parallel

Categoría `parallel` de la batería host (L1). El índice de categorías está en [../README.md](../README.md) y la taxonomía en [docs/testing/TAXONOMY.md](../../../docs/testing/TAXONOMY.md).

## Catálogo

| ID | Test | Qué cubre |
|----|------|-----------|
| HOST-137 | [parallel](137_parallel/README.md) | `engine/include/eng/parallel/parallel.hpp`: primitivas de concurrencia abstraídas (`hardware_threads`, `Thread`, `Mutex`, `Atomic`, `ConditionVariable`, `StopSource`/`StopToken`, `for_each_index`); no-ops en m68k, hilos reales en el host. |
