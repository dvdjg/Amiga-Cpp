# HOST-140: reglas de ajedrez 0x88, generación legal y perft

Test host de `engine/include/eng/board/rules/chess/` (`board.hpp`, `movegen.hpp`,
`fen.hpp`, `rules.hpp`): tablero 0x88, `make`/`unmake` con Zobrist incremental,
generación de jugadas legales (validada con **perft**) y fin de partida.

## Qué comprueba

1. FEN: round-trip de la posición inicial y de Kiwipete.
2. `make`/`unmake`: restauran tablero, estado y clave en todas las jugadas de
   Kiwipete.
3. Zobrist: la clave incremental coincide con `compute_key` en todo el árbol de
   profundidad 3 de una posición con promociones.
4. **perft** contra valores conocidos de la comunidad (cubre enroque, al paso y
   promoción):
   - inicial: 20 / 400 / 8902 / 197281;
   - Kiwipete: 48 / 2039 / 97862;
   - posición de al paso: 14 / 191 / 2812 / 43238;
   - posición de promociones: 44 / 1486 / 62379.
5. `terminal`: jaque mate, rey ahogado, material insuficiente (K vs K) y que K+R vs
   K no es tablas.
6. `ChessRules` cumple `GameRules` y da 20 jugadas legales en la posición inicial.

## Salida de referencia

```
Ajedrez 0x88:
OK: ajedrez 0x88 (fen, make/unmake, zobrist, perft, terminal, GameRules)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/140_chess_movegen
```
