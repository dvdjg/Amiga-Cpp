# HOST-193: rangos de manos y tabla preflop

Test host de `engine/include/eng/cards/eval/range.hpp`: las 169 clases canónicas de mano
inicial, el rango como conjunto de clases, el equity contra un rango y la tabla preflop.

## Qué comprueba

1. **169 clases**: las 1326 combinaciones de dos cartas caen en 169 clases distintas, el
   índice es simétrico, el round-trip por representante coincide, y la suma de
   combinaciones (6 por pareja, 4 suited, 12 offsuit) es 1326.
2. **Round-trip** de `starting_class`, `class_combo_cards` (cartas válidas y distintas) y
   límites de partición (parejas 0..12, suited 13..90, offsuit 91..168).
3. **`HandRange`**: rango completo = 169 clases / 1326 combinaciones; añadir AA = 6
   combinaciones.
4. **Tabla preflop** (`build_preflop_table`): AA > KK > 72o, AA > 800 ‰, y el top-4 por
   equity incluye AA.
5. **`equity_vs_range`**: contra el rango de solo AA el equity de AA baja respecto a un
   rango completo; 72o contra solo AA pierde casi siempre.
6. **`multiway_from_heads_up`**: más rivales ⇒ menos equity.

## Salida de referencia

```
eng::cards range:
OK: eng::cards range (169 clases, HandRange, equity vs rango y tabla preflop)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/166_cards_range
```
