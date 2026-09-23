# HOST-199: persona, rasgos de psique y arquetipos

Test host de `engine/include/eng/sim/{psyche_traits,archetypes,persona}.hpp`: la capa de
persona de `eng::sim`.

## Qué comprueba

1. **Defectos** (`Flaws`): bits, `has`/`set`/`clear`/`count` y nombre legible.
2. **Catálogo de arquetipos**: al menos 27, nombres únicos y `id`/nombre consistentes
   (`archetype_of(archetype_name(a)) == a`).
3. **Constelaciones coherentes**: el engreído (vanidad y dominancia altas, poca empatía,
   flemático, `overconfidence`), el pardillo (crédulo, sin compostura, mal farol) y el
   sabio (sereno e ilegible).
4. **`materialize`**: sin jitter copia el arquetipo (rasgos, aptitudes, defectos); con
   jitter es determinista por semilla y se mantiene en `[0, 100]`.
5. **Modificadores** de rasgo de psique (`trait_mod`: neutro 0, máximo +100, mínimo −100).

## Salida de referencia

```
eng::sim persona:
OK: eng::sim persona (rasgos de psique, aptitudes, defectos y arquetipos)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/sim/199_sim_persona
```
