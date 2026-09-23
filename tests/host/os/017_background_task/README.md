# HOST-017 — Tareas de fondo cooperativas

Valida en host el bloque `engine/include/eng/task/background.hpp`
(`eng::task::BackgroundQueue`): rutinas pesadas que avanzan **solo** cuando el bucle
principal no tiene nada que hacer, con indicadores baratos de progreso/rendimiento.

## Qué cubre

- **Tarea finita**: avanza por rebanadas, `permille` crece y termina quedando en `Done`
  (consultable) hasta que el juego la cancela; entonces el slot se libera y el handle
  queda invalidado (por la generación).
- **Tarea continua** (`total_units = 0`): no termina sola; `cancel()` la libera.
- **Contexto bidireccional**: la tarea recibe `vpos`/`frame`/`budget`/progreso/`avg` y
  **adapta su carga** (el test consume la mitad si el raster va tarde); el juego lee
  `progress()` (estado, `done`/`total`, `permille`, `remaining`, `avg`).
- **`task_abort`** marca la tarea como `Failed`.
- **Pool fijo** (`max_tasks`) y handles inválidos.
- **`permille_of`** sin desbordar `u32` (sin `u64`, freestanding).

## Integración en el engine

El `Engine` posee una `BackgroundQueue` y la expone en `GameContext::background`. La
drena en el hueco de VBlank (tarea ociosa que recibe `vpos`), como máximo
`max_slices_per_frame` rebanadas por frame: `update`/`render` siempre van primero.

## Ejecución

```bash
CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/017_background_task
```

Ver `docs/engine/architecture/BACKGROUND_TASKS.md`.
