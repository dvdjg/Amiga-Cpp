# HOST-194: variantes de póker (Omaha) y estructura Limit

Test host de `engine/include/eng/cards/rules/variants.hpp` y del evaluador
`evaluate_omaha` de `rules/hand_rank.hpp`, más la mesa en modo Limit.

## Qué comprueba

1. **Omaha 2+3**: sobre un tablero de cinco corazones, una mano sin dos corazones no
   puede hacer color; con dos corazones sí (color) y con 9-10 de corazones da escalera
   de color. La misma mano de 7 en Hold'em sí da la escalera de color del tablero.
2. **Ayudantes de variante**: `hole_cards_for` (2 en Hold'em, 4 en Omaha) y
   `limit_bet_size` (ciega pequeña en preflop/flop, grande en turn/river).
3. **Reparto por variante**: Hold'em reparte 2 cartas (la tercera queda `kNoCard`);
   Omaha reparte 4 válidas y distintas.
4. **Limit**: la subida es de tamaño fijo (apuesta viva + ciega pequeña), no se ofrece
   all-in y se respeta el tope `kLimitMaxRaises`; No-Limit sí ofrece all-in.
5. **Showdown Omaha**: con el tablero de escalera de color real, gana quien tiene la
   escalera de color (2 hole + 3 board), no el trío de ases.

## Salida de referencia

```
eng::cards variants:
OK: eng::cards variants (Omaha 2+3, reparto, Limit fijo y tope)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/cards/194_cards_variants
```
