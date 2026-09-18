# HOST-161: núcleo de `eng::cards`

Test host de `engine/include/eng/cards/core/{types,deck,budget}.hpp`: tipos de carta, baraja
determinista y presupuesto de memoria de los juegos de naipes.

## Qué comprueba

1. `Suit`/`Rank` y empaquetado de `Card` (`rank << 2 | suit`), centinela `kNoCard`.
2. `Deck`: orden identidad, reparto desde el final, retirada de cartas conocidas y que
   **mismo PRNG + semilla ⇒ misma permutación** con las 52 cartas únicas.
3. `CardPlan`: `N20` para 20 kB (sin Monte Carlo) y `N512` para 512 kB, con
   `planned_bytes() <= free_bytes`, y crecimiento del plan con la RAM.
4. Nombres de categoría/rango/palo para UI.

## Salida de referencia

```
eng::cards core:
OK: eng::cards core (tipos, baraja determinista, presupuesto N20-N512)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/161_cards_core
```
