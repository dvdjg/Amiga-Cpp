# HOST-143: buscador adversario (negamax + alpha-beta + quiescence)

Test host de `eng/board/search/search.hpp` a través de `eng::board::ChessSearcher`.

## Qué enseña / comprueba

Progresión de la búsqueda, en el orden en que se estudia:

1. **Negamax**: el valor de una posición es el máximo de los valores negados de las
   respuestas del rival (todo desde el bando al turno).
2. **Alpha-beta**: ventana `[alpha, beta]`; superar `beta` corta la rama.
3. **Iterative deepening**: profundidades 1, 2, 3… guardando la mejor de cada nivel;
   se puede interrumpir sin perderlo todo y ordena la raíz con la mejor anterior.
4. **Quiescence**: al llegar al límite se siguen buscando capturas (y todas las
   jugadas si hay jaque) para no evaluar a mitad de un intercambio.

Casos verificados: mate en 1, posición inicial equilibrada a profundidad 3, dama
colgada capturada, recaptura resuelta por quiescence, y los presupuestos (límite de
nodos → `aborted` conservando la mejor jugada; `StopToken` → cancelación limpia).

## Salida de referencia

```
Ajedrez: buscador:
OK: buscador (mate en 1, equilibrio, captura, quiescence, presupuesto, stop)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/143_chess_search
```
