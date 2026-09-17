# HOST-094: restauración de la costura en `scroll_engine` (ScopeGuard)

Test host de `engine/include/eng/field/scroll_engine.hpp`: cuando una función de scroll
(`scroll_right`, `scroll_left`, `scroll_down`…) guarda la costura con `save_word` y un
`add_draw` posterior **rechaza el frame** (devuelve `false`), la costura debe quedar
**restaurada** (`restore_saveword`). Antes del fix se hacía `return false` sin restaurar y
la word de costura quedaba a medio escribir.

## Qué comprueba

1. `scroll_right`/`scroll_left` devuelven `false` si `add_draw` falla, **después** de haber
   guardado la costura.
2. En ese camino de error se llama `restore_saveword` (el `ScopeGuard` la restaura).
3. En el **camino correcto** NO se restaura (el guard se libera).

Usa un sink de pega que falla el primer `add_draw` posterior al primer `save_word`
(determinista, sin depender de contar blits), y uno que nunca falla para el caso OK.

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/094_scroll_saveword_guard
```
