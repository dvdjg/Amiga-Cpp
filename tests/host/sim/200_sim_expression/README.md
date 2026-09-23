# HOST-200: expresión no verbal y fuga

Test host de `engine/include/eng/sim/expression.hpp`: canales y gestos, control por gesto,
compostura efectiva y el cálculo determinista de la fuga.

## Qué comprueba

1. **Tabla de gestos**: los canales autonómicos (pupilas, fosas nasales, temblor) tienen
   control bajo y los volitivos (mirada desafiante, sonrisa) más alto; canal correcto de
   sonrisa, bostezo y *timing*; catálogo amplio (> 60 gestos).
2. **Compostura efectiva**: base + pericia sube, fatiga/tilt bajan, nunca negativa.
3. **Fuga**: sin emoción no hay fuga; más compostura ⇒ menos fuga; el canal autonómico
   delata más que el volitivo; con compostura perfecta no se filtra nada; la ansiedad sube
   la fuga.
4. **Lista de fugas** (`compute_leaks`): incluye los gestos visibles (el temblor entre
   ellos) y queda vacía con compostura perfecta.

## Salida de referencia

```
eng::sim expression:
OK: eng::sim expression (canales, control, compostura y fugas)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/200_sim_expression
```
