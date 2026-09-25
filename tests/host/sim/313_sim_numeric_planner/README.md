# HOST-313: GOAP numérico en el ecosistema (`eng::sim`)

Test host de `engine/include/eng/sim/planner.hpp` y `world.hpp` con el **dominio GOAP
numérico** (`eng::ai::NumericGoap`). Comprueba que la capa de planificación del ecosistema
es **genérica sobre el dominio**: `PlannerDriver` y `SimWorld` aceptan por plantilla tanto
el booleano ligero por defecto (`SimGoap`, 0 variables) como el numérico
(`SimNumericGoap<N>`, hechos + `N` magnitudes). El algoritmo no se reimplementa.

## Qué comprueba

1. **`PlannerDriver<..., SimNumericGoap<2>>`**: plan con precondiciones/efectos numéricos
   (`forage` consigue comida, `eat` exige comida y baja el hambre 6 → 1), coste óptimo 2,
   plan vacío válido si el objetivo ya se cumple y fallo controlado (`false`) cuando
   ninguna acción alcanza el objetivo.
2. **`SimWorld<..., SimNumericGoap<2>>`**: el mundo propaga el dominio por el último
   parámetro de plantilla; `replan`/`has_plan`/`current_action`/`advance_plan`/`abort_plan`
   funcionan con estado y acciones numéricas.
3. **Presupuesto *anytime*** (`set_budget`/`partial`): con límite 2, el planner numérico
   devuelve un plan parcial (más corto, respeta el límite) y el `PlannerDriver` lo expone
   igual; sin límite, el plan completo de 10 pasos.

El caso booleano por defecto no cambia (lo cubre HOST-155). El dominio (var 0 = hambre,
var 1 = energía) es de ejemplo; el juego define el suyo.

## Salida de referencia

```
Sim numeric planner:
OK: Sim numeric planner (dominio GOAP por plantilla, driver y mundo)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/313_sim_numeric_planner
```
