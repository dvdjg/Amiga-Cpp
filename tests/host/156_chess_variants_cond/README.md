# HOST-156: variantes de condición (King of the Hill / Three-check)

Test host de `eng/board/rules/chess/{variant,rules}.hpp` (hook `variant_score`).

## Qué enseña / comprueba

Además de Chess960 (posición inicial), hay variantes que cambian la **condición de
victoria**; se implementan con `variant_score`, que el buscador consulta en cada nodo:

- **King of the Hill**: gana quien lleva el rey a una de las 4 casillas centrales. El
  test da Kc4 con el rey negro lejos y comprueba que la búsqueda juega **Kd4** con
  puntuación de mate.
- **Three-check**: gana quien da 3 jaques. El test parte de 2 jaques ya dados y
  comprueba que la búsqueda encuentra el tercero (Qd8+) como mate.
- El ajedrez estándar/960 devuelve `variant_score == 0` y no se ve afectado.

## Salida de referencia

```
Ajedrez: variantes de condicion:
OK: King of the Hill y Three-check (variant_score) sin afectar al standar
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/156_chess_variants_cond
```
