# HOST-197: Seven-Card Stud

Test host de `engine/include/eng/cards/rules/seven_stud.hpp`: reglas de Seven-Card Stud
Limit (ante, bring-in, cartas privadas/descubiertas, cinco calles y showdown).

## Qué comprueba

1. Arranque con 4 asientos: ante (4) + bring-in (2) en el bote, 3 cartas por asiento,
   calle 3.ª y acciones legales (fold/call/raise).
2. Mano completa con check/call: termina, cada jugador vivo llega a 7 cartas, sin bucle
   infinito y con conservación de fichas.
3. Resolución por retirada: gana el único que queda y se conservan las fichas.
4. La subida de la 3.ª calle usa la apuesta pequeña (4) sobre la apuesta viva.

## Salida de referencia

```
eng::cards stud:
OK: eng::cards stud (bring-in, 5 calles, showdown y retirada)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/cards/197_cards_stud
```
