# HOST-184: conocimiento consumido por el motor (libro + finales)

Test host de `eng/board/rules/chess/opening.hpp` y la evaluación con finales.

## Qué enseña / comprueba

Cierra el ciclo del conocimiento construido antes:

- **Libro de aperturas**: `probe_opening_book(pos, book)` consulta el libro por la
  clave Zobrist de la posición y devuelve la jugada (y el nombre). El test construye
  un libro con 1.e4 y comprueba que la posición inicial está en él.
- **Finales teóricos en la evaluación**: `evaluate` reconoce finales ganados conocidos
  (KRK/KQK/KBNK/KQvKR) y devuelve una ventaja grande (±1500) en lugar de la evaluación
  posicional, para que la búsqueda vaya directa a la conversión. El test comprueba
  K+R vs K desde ambos bandos.

## Salida de referencia

```
Ajedrez: conocimiento en el motor:
OK: libro de aperturas (sonda) y finales en la evaluacion
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/157_chess_knowledge
```
