# HOST-112: Utility AI

Test host de `engine/include/eng/ai/decision/utility.hpp`: puntuar opciones de decisión
con una media ponderada de consideraciones normalizadas (`Utility`) y elegir la mejor
(`UtilitySelector<MaxOptions>`). Todo entero y determinista (sin `float`).

## Qué comprueba

1. `Utility`: media ponderada en `[0, 1000]`, recorte de valores fuera de rango y pesos
   no positivos ignorados.
2. `UtilitySelector`: mejor puntuación, empate → índice menor, capacidad y vacío
   (`no_option`).
3. Caso de uso: un guardia elige atacar o huir según distancia, munición y salud.

## Salida de referencia

```
Utility:
OK: Utility (media ponderada, selector, decision del guardia)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/112_utility
```
