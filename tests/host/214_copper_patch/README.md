# HOST-214: `copper::PatchHandle` (MOVE parcheable por frame)

Test host de `eng::copper::PatchHandle` (`eng/graphics/copper/scheduler.hpp`): el handle
tipado a un MOVE de la copperlist para parchearlo por frame con precisión quirúrgica, sin
offsets cableados. Es el mecanismo de variabilidad en runtime del modelo de composición
(`SCENE_COMPOSITION.md`).

## Qué comprueba

1. `Scheduler::patchable(reg, value)` emite un MOVE y devuelve un handle válido.
2. El MOVE inicial queda en la lista (word de valor).
3. `handle.set(value)` reescribe el word de valor.
4. Un handle por defecto es inválido y `set()` no hace nada.

## Salida de referencia

```
OK: copper::PatchHandle (MOVE parcheable por frame).
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/214_copper_patch
```
