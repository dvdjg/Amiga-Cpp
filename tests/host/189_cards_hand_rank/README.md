# HOST-189: evaluador de manos de póker

Test host de `engine/include/eng/cards/rules/hand_rank.hpp`: evaluador de 5 cartas y "mejor
de 5 entre 7" por conteo de rangos y palos.

## Qué comprueba

1. Categorías conocidas: escalera de color, póker, full, color, escalera (incluida la de as
   bajo A-2-3-4-5), trío, doble pareja, pareja y carta alta.
2. Orden total entre categorías con el `HandValue` empaquetado.
3. Mejor de 7: gana color sobre escalera y se detecta el full; con menos de 5 cartas no hay
   valor.
4. Desempate por *kicker* y empate exacto.

## Salida de referencia

```
eng::cards hand_rank:
OK: eng::cards hand_rank (categorias, orden, mejor de 7, kickers)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/162_cards_hand_rank
```
