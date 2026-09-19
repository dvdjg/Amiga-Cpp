# HOST-191: equity Monte Carlo

Test host de `engine/include/eng/cards/eval/equity.hpp`: estimación de equity contra rivales
aleatorios, heurística preflop y pot odds.

## Qué comprueba

1. Heurística preflop: AA > KK > 72o, con AA muy fuerte y 72o débil.
2. Equity contra un rival aleatorio: AA supera a 72o y está por encima del 75 %; misma
   semilla ⇒ mismo resultado (determinismo).
3. Mano hecha (escalera de color real) contra 2 rivales gana el 100 % de las muestras.
4. Pot odds: 100 sobre un bote de 300 = 250 ‰; sin llamada = 0; 50 sobre 50 = 500 ‰.

## Salida de referencia

```
eng::cards equity:
OK: eng::cards equity (Monte Carlo determinista, heuristica y pot odds)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/164_cards_equity
```
