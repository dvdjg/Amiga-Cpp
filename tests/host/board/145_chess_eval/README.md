# HOST-145: evaluación de ajedrez y rasgos de desarrollo

Test host de `eng/board/eval/chess_eval.hpp` y `eng/board/eval/features.hpp`.

## Qué enseña / comprueba

La evaluación (`ChessEval`) suma, en centipeones y sin `float`:

```
material + PST (centralización) + movilidad + estructura de peones + desarrollo/rey
```

`evaluate` devuelve la puntuación **desde el bando al turno** (negamax). Por eso:

- la posición inicial vale exactamente 0 (todo simétrico);
- tras 1.e4, que mejora a las blancas, la puntuación desde el punto de vista de las
  negras es negativa;
- un caballo en e4 vale más que uno en a1 (PST de centralización).

`extract_development` (fuente compartida con el futuro explicador NLG) se valida en
el inicio (4 menores y 3 mayores sin mover), tras 1.Cf3 (`3` menores), con dama
prematura (1.e4 e5 2.Qh5) y con reyes en el centro en la jugada 12.

## Salida de referencia

```
Ajedrez: evaluacion:
OK: evaluacion (simetria, centralizacion, rasgos de desarrollo)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/board/145_chess_eval
```
