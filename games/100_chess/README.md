# `games/100_chess` — ajedrez sobre el engine

Primer consumidor real de `eng::board`. El jugador lleva las **blancas** con el
joystick (puerto 0) y el motor juega las **negras**.

## Controles

- **Joystick**: mueve el cursor por el tablero.
- **Fuego**: la primera pulsación selecciona la pieza blanca bajo el cursor; la
  segunda en el destino hace la jugada (promoción a dama por defecto). Fuego sobre
  una casilla inválida cancela la selección.

## Qué motor usa

- `eng::board::ChessRules`: reglas legales, jaque/mate, ahogado, 50 movimientos,
  material insuficiente y repetición (3×) con `PositionHistory`.
- `ChessSearcher`: negamax + alpha-beta + iterative deepening + quiescence + TT
  (profundidad 2, presupuesto de nodos) elige la jugada del motor.
- `eng::board::chess::explain`: describe la posición en español con los rasgos de la
  evaluación (se muestra en la barra inferior, junto a la última jugada del motor).
- **Variantes**: arranca en **Chess960** (piezas descolocadas) de forma reproducible
  (`kVariant`/`kSeed` en `main.cpp`); `Standard` es cambiar esas dos constantes. El
  enroque generalizado cubre las torres en columnas arbitrarias.

## Visual

`StaticEhbScene` (320×256, 6 planos EHB). El tablero (8×8 casillas de 24×24), las
piezas (letras `K Q R B N P` de `Font8`) y el estado se rasterizan a los bitplanes
por CPU (patrón de la demo 060): el tablero solo se redibuja cuando hay jugada o se
mueve el cursor, así que no hace falta Blitter ni doble buffer.

## Build / run / analyze

```bash
bash tools/build/build-demo.sh games/100_chess --debug --clean
bash tools/run/run-demo.sh games/100_chess
bash tools/analyze/analyze-demo.sh games/100_chess
```

## Estado

- **Compila y enlaza** con el flujo canónico (`build-demo.sh`, verificado).
- **Ejecutado en WinUAE** (`run-demo.sh --warp`): alcanza READY y captura
  `out/run/100_chess/A500_debug/screenshot.png`; `analyze-demo.sh` da **OK** (tablero
  dibujado: hay píxeles no azules). Evidencia build → run → analyze.
- **Pendiente**: pulido visual (piezas dibujadas, colores), reloj por jugador,
  selección con ratón y una `analyze-sequence.sh` propia del juego.
