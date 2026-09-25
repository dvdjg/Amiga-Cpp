# HOST-314: GOAP con presupuesto *anytime* (`eng::ai::Goap`)

Test host de la búsqueda **acotada** de `engine/include/eng/ai/planning/goap.hpp`. Con
`set_budget(n)` el planner expande como máximo `n` nodos y, si no alcanza el objetivo,
devuelve el mejor **parcial** en vez de fallar. Pensado para repartir CPU por tick en el
68000: cada tick avanza el plan un poco en vez de bloquear.

## Qué comprueba

1. **Sin presupuesto** (`budget() == 0`, sin límite): plan completo de 7 pasos,
   `partial()` falso.
2. **Con presupuesto corto** (`set_budget(2)`): `found()` falso, `partial()` verdadero, el
   parcial es **prefijo** del plan completo (menor `h`; a igual `h`, el de mayor avance),
   no alcanza el objetivo y respeta el límite (`expansions() <= 2`).
3. **Sin solución y sin presupuesto**: 0 acciones y `partial()` falso (el parcial solo
   aplica si el presupuesto **corta** la búsqueda, no si el espacio se agota).
4. **Con presupuesto amplio**: de nuevo el plan completo, no parcial.

El parcial **no se cachea** (solo se cachea un plan que alcanza el objetivo) y no
garantiza la solución: el llamador decide si lo ejecuta o espera más presupuesto. Sin
presupuesto el comportamiento es idéntico al histórico (HOST-107).

## Salida de referencia

```
GOAP anytime:
OK: GOAP con presupuesto (parcial vs. completo, prefijo y limite)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/ai/314_goap_anytime
```
