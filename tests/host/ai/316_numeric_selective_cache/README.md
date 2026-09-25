# HOST-316: caché selectiva (hechos + variables) y LRU del GOAP numérico (`eng::ai::NumericGoap`)

Test host de `invalidate_selective` y la política LRU de la caché de planes del planner
numérico (`engine/include/eng/ai/planning/numeric_goap.hpp`). Cada entrada guarda sus
**dependencias** (`used_facts` = unión de precondiciones/efectos de hechos; `used_vars` =
máscara de variables que intervienen) y un contador `hits`.

## Qué comprueba

1. **Invalidación por hecho**: un hecho que el plan usa lo invalida (se replanifica); uno
   que no usa (u otro plan que no lo toca) sobrevive.
2. **Invalidación por variable**: análoga con `used_vars`.
3. **Cambio irrelevante**: un hecho/variable que no aparece en los planes no invalida nada.
4. **LRU+menos-usos**: con la caché llena (`MaxCachedPlans=2`), al insertar una tercera
   consulta se desaloja la entrada con menos `hits`; la más usada sobrevive y la desalojada
   se replanifica.

`clear_plan_cache()` sigue siendo el vaciado total. El pool de acciones se recompacta al
invalidar o desalojar.

## Salida de referencia

```
GOAP numeric selective cache:
OK: GOAP numerico (invalidacion selectiva por hecho/variable y LRU)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/ai/316_numeric_selective_cache
```
