# HOST-182: Go completo (pase, superko y apertura)

Test host de `eng/board/rules/go/` y `eng/board/knowledge/patterns.hpp`.

## Qué enseña / comprueba

- **Pase / dos pases**: pasar es legal; dos pases consecutivos terminan la partida
  (`is_over` y `terminal == GameEnded`).
- **Superko**: `position_repeated` compara la clave de una jugada con la posición actual
  y el historial (32 claves); una jugada que recrea una posición anterior es ilegal.
- **Apertura (fuseki)**: `opening_move` propone un punto estrella (4-4) libre y evita
  los ocupados (base de un banco de patrones futuro).

## Salida de referencia

```
Go 9x9: pase, superko y apertura:
OK: Go (pase/dos pases, superko y patrones de apertura)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/board/182_go_complete
```
