# HOST-180: evaluación y búsqueda de Go 9×9

Test host de `eng/board/eval/go_eval.hpp` y `eng/board/rules/go/rules.hpp`.

## Qué enseña / comprueba

El mismo buscador genérico del engine juega al Go cambiando solo la policy
(`GoRules` + `GoEval` + `GoOrdering`):

- **Evaluación**: territorio (regiones vacías rodeadas por un solo color, por
  *flood fill*) + capturas + penalización de grupos en **atari**; desde el bando al
  turno. El tablero vacío vale 0.
- **Búsqueda** (`GoSearcher` = `Searcher<GoRules, GoEval, GoOrdering>`): encuentra la
  **captura** de una piedra en atari (profundidad 2) y en tablero vacío devuelve una
  jugada legal completando al menos una profundidad.

## Salida de referencia

```
Go 9x9: evaluacion y busqueda:
OK: Go (territorio, captura por busqueda y jugada legal)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/board/180_go_search
```
