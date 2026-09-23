# HOST-198: Five-Card Draw (+ Deuces Wild)

Test host de `engine/include/eng/cards/rules/five_draw.hpp` y de
`evaluate_deuces_wild` (`hand_rank.hpp`).

## Qué comprueba

1. `evaluate_deuces_wild`: dos doses + tres ases = póker de ases; un dos completa una
   escalera; se puede cambiar el rango comodín (treses wild).
2. Arranque: 5 cartas por asiento, ante en el bote y acciones legales (check/apuesta).
3. Mano completa con check/call y descarte (`draw_take` + `recommended_draw_mask`):
   termina, conserva las fichas y no entra en bucle.
4. Mano completa con **Deuces Wild** activo (`deuces_wild = true`).

## Salida de referencia

```
eng::cards five_draw:
OK: eng::cards five_draw (deuces wild, descarte y showdown)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/cards/198_cards_five_draw
```
