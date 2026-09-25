# HOST-321: invalidación selectiva de la caché de planes (`eng::ai::Goap`)

Test host de `invalidate_selective` en `engine/include/eng/ai/planning/goap.hpp`. Cuando
cambia un hecho del dominio, `plan_cached` no necesita vaciar toda la caché: basta con
descartar las entradas cuyo plan **depende** de ese hecho (lo requiere como precondición o
lo modifica como efecto). El resto sigue siendo válido.

## Qué comprueba

1. **Acierto**: una consulta repetida a `plan_cached` no vuelve a expandir.
2. **Invalidación irrelevante**: un hecho que el plan no usa no lo invalida (sigue el
   acierto).
3. **Invalidación relevante**: un hecho que el plan usa (precondición/efecto) lo invalida
   y la siguiente consulta vuelve a planificar.
4. **Compactación**: con dos planes cacheados, invalidad uno conserva el otro —su plan se
   compacta en el pool y sigue acertando con el contenido intacto— mientras el descartado
   se replanifica.

`clear_plan_cache()` sigue siendo el vaciado total. La entrada guarda `used_facts` (unión
de precondiciones y efectos de las acciones del plan), calculada al insertar.

## Salida de referencia

```
GOAP selective cache:
OK: GOAP invalidacion selectiva (dependencias, cache y compactacion)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/ai/315_goap_selective_cache
```
