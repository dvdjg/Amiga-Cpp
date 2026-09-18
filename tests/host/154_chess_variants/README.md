# HOST-154: variantes (Chess960) y torneos rápidos

Test host de `eng/board/rules/chess/variant.hpp` y `eng/board/tournament.hpp`.

## Qué enseña / comprueba

- **Chess960 / Fischer Random** ("piezas descolocadas"): `chess960_back_rank` genera
  las 960 disposiciones válidas (alfiles en colores opuestos, rey entre torres); el
  test comprueba las 960.
- **Enroque generalizado**: con rey en b1 y torre en h1, el enroque corto lleva el rey
  a g1 y la torre a f1; `make`/`unmake` restauran tablero y clave.
- **Torneo rápido** (`tournament.hpp`): `play_game` juega una partida con presupuesto
  de nodos por jugada; `arena_chess` juega N partidas con arranques de variante y
  semilla creciente, devolviendo el marcador.

## Salida de referencia

```
Ajedrez: variantes y torneos:
OK: Chess960 (960 disposiciones, enroque generalizado) y torneo rapido
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/154_chess_variants
```

Herramienta de torneo: `tools/board/arena.sh [games] [depth] [variant] [seed] [max_plies] [nodes]`.
