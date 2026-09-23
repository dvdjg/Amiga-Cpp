# HOST-161: memoria de corto y largo plazo (`eng::sim`)

Test host de `engine/include/eng/sim/memory.hpp`: cómo la percepción entra en la memoria de
trabajo y se consolida (o se olvida).

## Qué comprueba

1. **`integrate_observations`**: las observaciones alimentan los `Tracker` (corto plazo),
   **acumulando las modalidades** que detectaron al objetivo y conservando la mayor
   saliencia; la recurrencia la realza.
2. **`working_strength`**: fuerza en corto plazo de un objetivo.
3. **`forget_working`**: olvido rápido del corto plazo (`decay`).
4. **`consolidate`**: lo suficientemente fuerte o saliente pasa a largo plazo
   (`KnowledgeSet`), con el **mapeo de categorías** (`Threat→Enemy`, `Den→Shelter`...) y el
   sujeto correcto (entidad o **región**); lo débil no se consolida.
5. **`recall`/`recalls_shelter`/`recalls_danger`**: lectura del largo plazo.
6. **Integración en el mundo**: `sense` → `integrate_senses` → `consolidate_memory`
   (y `tick_memory`).

## Salida de referencia

```
Sim memory:
OK: Sim memory (corto plazo, olvido, consolidacion, largo plazo)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/sim/161_sim_memory
```
