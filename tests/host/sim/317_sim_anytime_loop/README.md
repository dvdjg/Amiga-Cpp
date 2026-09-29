# HOST-317: bucle de planificación *anytime* por frames (`eng::sim`)

Test host de `planning/goap.hpp` + `sim/planner.hpp` como **consumidor real** del presupuesto
*anytime*. Una criatura encadena acciones (`0 → 1 → … → 5`) y no puede gastar toda la
búsqueda en un frame: en cada frame pide un plan con presupuesto (`apply_budget`) y, si sale
**parcial**, ejecuta un paso y sigue.

## Qué comprueba

1. **Alcanza el objetivo** repartiendo el trabajo en varios frames (`frames > 1`).
2. **Ejecuta los 5 pasos** del plan completo.
3. **Ninguna búsqueda supera `PlanParams::budget`** — el coste por frame queda acotado, que
   es lo que permite planificar dentro del presupuesto de CPU del 68000.

Sin presupuesto el bucle sería una sola búsqueda (HOST-107); con él, el plan se reparte y el
parcial permite empezar a actuar antes de tener el plan completo.

## Salida de referencia

```
Sim anytime loop:
OK: Sim bucle anytime (parciales por frame, pasos y presupuesto)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/317_sim_anytime_loop
```
