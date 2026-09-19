# HOST-168: comodines (jokers)

Test host del soporte de **comodines** en `eng/cards`: mazo de 54 cartas (52 + 2 jokers),
sustitución en el evaluador, heurística preflop y showdown.

## Qué comprueba

1. `Deck::reset(true)` da 54 cartas e incluye los dos comodines; `reset()` da 52 y no los
   incluye; `card_is_joker` y `card_playable`.
2. `evaluate_hand` con comodines: uno completa escalera de color, dos también, y uno
   completa el póker de ases; sin comodines el resultado es idéntico a `evaluate_plain`.
3. Con menos de 5 cartas no hay valor.
4. `preflop_strength_permille` trata el comodín como as.
5. Showdown con un comodín en la mano: gana la pareja de ases real.

## Salida de referencia

```
eng::cards wildcards:
OK: eng::cards wildcards (mazo 54, sustitucion y showdown)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/168_cards_wildcards
```
