# HOST-155: planificación GOAP para criaturas (`eng::sim`)

Test host de `engine/include/eng/sim/planner.hpp` y `domain.hpp`: la capa que conecta el
planificador genérico `eng::ai::Goap` con el modelo de criatura. No reimplementa el
algoritmo; aporta la **decisión de planificar**, la **ejecución del plan** por ticks y un
**dominio de ejemplo** (objetos y construcción).

## Qué comprueba

1. **`plans`**: la decisión de planificar sale de la personalidad (curiosidad y autonomía)
   con umbrales paramétricos (`PlanParams`).
2. **`should_replan`** (histéresis): entra al superar los umbrales de entrada, respeta el
   intervalo entre replanes, se **mantiene** dentro de la banda (`exit_*` < `min_*`) y sale al
   bajar de los de salida. **`apply_budget`** vuelca `PlanParams::budget` al conductor
   (presupuesto *anytime*: con `budget = 1`, `replan` deja un plan parcial y no pasa del límite).
3. **`PlanRunner`**: cargar un plan, consumir pasos por tick (`current`/`advance`),
   terminar (`done`) y abortar; un plan que no cabe en `MaxSteps` se rechaza.
4. **`PlannerDriver`**: replanificar con `Goap` (plan óptimo de tres acciones encadenadas,
   coste 3), plan vacío válido cuando el objetivo ya se cumple, y fallo controlado
   (`false`) cuando no hay solución. Comprueba `plan_cost` y `expansions`.
5. **Dominio de construcción** (`domain.hpp`): desde cero, conseguir refugio exige
   `Gather → CraftTool → Gather → Build` (fabricar consume material, así que hay que
   recoger dos veces; coste 4); comer exige `Forage → Eat`; con materiales y herramienta
   previos basta `Build` (coste 1). `SimInventory` traduce el estado del agente a hechos y
   `action_kind_of` mapea cada paso a una acción ejecutable (que `object.hpp` materializa,
   HOST-157).

El dominio es de ejemplo; el juego define el suyo. El planificador de `Goap` ocupa miles
de bytes y debe instanciarse en memoria estática o de fondo (lo documenta la cabecera).

## Salida de referencia

```
Sim planner:
OK: Sim planner (decision, PlanRunner, PlannerDriver GOAP, dominio de construccion)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/155_sim_planner
```
