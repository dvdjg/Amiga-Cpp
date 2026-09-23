# HOST-141: repetición (3×) y finales teóricos

Test host de `eng/board/rules/chess/history.hpp` y `endgame.hpp`.

## Qué enseña / comprueba

- **Historial y repetición:** la clave Zobrist incluye turno, enroques y al paso.
  El "bailoteo" Nf3 Nf6 Ng1 Ng8 vuelve a la posición inicial; a la segunda vuelta
  son tres apariciones y `terminal(pos, history)` devuelve `Repetition`. La ventana
  de búsqueda hacia atrás es `halfmove + 1` (una repetición no cruza una captura ni
  un peón), y un movimiento de peón abre una ventana nueva.
- **Finales por material:** K vs K, K+B vs K, K+B vs K+B del mismo color son tablas;
  K+R vs K y K+Q vs K están ganados.
- **K+P vs K con la regla del cuadrado:** el rey defensor alcanza la promoción si su
  distancia de Chebyshev a la casilla de coronación no supera los avances del peón
  (rey en e8 → tablas; rey en h1 → corona).

## Salida de referencia

```
Ajedrez: repeticion y finales:
OK: ajedrez (repeticion 3x, ventana, finales teoricos, regla del cuadrado)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/141_chess_draw_endgame
```
