# HOST-192: simulación de partidas entre bots

Test host de `engine/include/eng/cards/ai/bot.hpp` y `engine/include/eng/cards/sim/session.hpp`:
sesiones completas CPU vs CPU para ajustar el nivel.

## Qué comprueba

1. Sesión de 6 asientos y 120 manos con estilos variados: se juegan todas, la suma de net es
   cero (conservación de fichas), hay subidas, igualadas y showdowns, y cada mano termina por
   showdown o por retirada.
2. Determinismo: misma semilla ⇒ mismos resultados; semilla distinta ⇒ reparto distinto.
3. Perfil `N20` (sin Monte Carlo): misma conservación y actividad jugando manos.
4. Cálculo de `bb/100` a partir del net por asiento.

## Salida de referencia

```
eng::cards selfplay:
OK: eng::cards selfplay (conservacion, determinismo, N20/N512)
```

## Ejecutar

```bash
CXX="/c/.../g++.exe" bash tools/run-host-tests.sh tests/host/165_cards_selfplay
```
