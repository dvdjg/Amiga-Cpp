# HOST-190: reglas de Texas Hold'em

Test host de `engine/include/eng/cards/rules/texas_holdem.hpp`: reparto, ciegas, acciones
legales, rondas de apuestas, resolución por retirada y botes laterales.

## Qué comprueba

1. Reparto con 3 asientos: ciegas 5/10, bote 15, habla UTG y las hole cards son válidas y
   distintas.
2. Acciones legales con apuesta viva: `Fold`, `Call`, `Raise`, `AllIn` y **no** `Check`.
3. Mano completa de 2 jugadores con check/call hasta el showdown: termina, llega a 5
   comunitarias y conserva las fichas (suma de stacks = 2 000).
4. Resolución por retirada: gana la ciega grande y la pequeña pierde su ciega.
5. **Botes laterales**: tres all-in de 100/200/300 con AA/KK/QQ cobran 300/200/100.
6. Empate: un bote con dos manos iguales se divide a partes iguales.

## Salida de referencia

```
eng::cards texas_holdem:
OK: eng::cards texas_holdem (reparto, calles, retirada y botes laterales)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/cards/190_cards_holdem
```
