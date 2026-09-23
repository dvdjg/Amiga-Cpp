# HOST-186: GOAP numérico con heurística relajada y memo

Test host de `engine/include/eng/ai/planning/numeric_goap.hpp` (fase B).

## Qué enseña / comprueba

Sobre el GOAP numérico (HOST-185) añade una **heurística de grafo relajado**: `h_max`
sobre los hechos (ignorando los efectos de borrado) más una cota numérica por el mayor
delta por acción. Los valores `h` se **memoizan por estado entre llamadas** (caché de
búsquedas previas), de modo que replanificar con el mismo dominio reutiliza trabajo.

- `plan_relaxed` encuentra un plan válido que cumple el objetivo.
- El memo de heurística se reutiliza en una segunda consulta (`heuristic_hits` crece).
- `clear_heuristic_cache` lo vacía.

## Salida de referencia

```
GOAP numerico relajado:
OK: GOAP numerico con h relajada y memo de heuristica
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/ai/186_goap_numeric_relaxed
```
