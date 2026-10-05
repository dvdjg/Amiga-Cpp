# HOST-411 — convergencia planner ↔ `RasterLayout` (§7)

`eng/scene/raster_plan.hpp` (`eng::scene::plan_raster_layout`): construye un `RasterLayout` (la
composición general de pantalla por bandas) desde un `ScenePlan` declarativo + una **vista por capa**.
Es el puente que hace el vocabulario del planner reutilizable en el camino de **bajo nivel**
(`RasterLayout`, demos de técnica).

## Qué comprueba

- Estrategia `Single` → 1 banda; `Dpf` → 1 banda **dual**; `Bands` → N bandas en el `top` de cada
  colocación.
- Errores: faltan vistas (`NoViews`), estrategia sin mapeo (`UnsupportedStrategy`).

## Por qué es reutilizable

`RasterLayout` sirve a **cualquier** juego que componga la pantalla por bandas (DPF, split-screen,
franjas mode-switch, HUD). Emitirlo desde el mismo `ScenePlan` unifica el vocabulario entre el
camino de escena (`XlimitedScene`) y el de técnica (`RasterLayout`) — lo usan ya las demos 127 (DPF)
y 129 (split-screen).
