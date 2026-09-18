# `demos/amiga/123_chess_match` — Partida de ajedrez entre dos motores

Demo de partida completa y autónoma entre **dos motores de ajedrez** del engine
(`eng::board::chess`). No hay entrada de usuario: las blancas y las negras juegan
solas, cada una con su propia instancia de posición y de buscador, y un **juez**
independiente narra la partida.

## Qué muestra

- Dos jugadores con **estilos distintos**: las blancas usan pesos agresivos
  (`aggressive_weights()`) y las negras pesos posicionales (`positional_weights()`),
  aplicados por `StyledEval` a través de una variable global de pesos activos.
- **Instancias independientes**: cada jugador mantiene su `Position`, su historial y
  su propio `Searcher`; el juez mantiene una tercera posición. Las jugadas se aplican
  a las tres copias con la misma `Move`.
- **Libro de aperturas en memoria** (`build_book()` sobre `probe_opening_book`): las
  blancas consultan el libro y responden al instante con `BookDelay`; el juez nombra
  la apertura con `book_name_of`.
- **Juez narrador** (`eng::board::explain` + características de desarrollo): anuncia
  cada jugada en notación algebraica y comenta mate, jaque, aperturas, dama
  prematura, retraso de desarrollo, rey en el centro, capturas, estructura de peones
  y enroques.
- **Relojes** de 5:00 con incremento, y **búsqueda por rebanadas** (200 nodos por
  frame, ~20 frames por jugada) para no bloquear el frame.
- **Última jugada resaltada en amarillo** (casillas de origen y destino) y marca
  `PENSANDO...` en el panel del bando que piensa.

## Distribución en pantalla (320×256, EHB)

```text
+------------------------+-----------------------------+
|                        |  BLANCAS agresivo           |
|   tablero 8x8 (128 px) |  Prof / Eval / Nodos ...    |
|   + ultima jugada      |  Mejor <jugada>             |
|     en amarillo        |  PENSANDO...                |
|                        |-----------------------------|
|  JUEZ                  |  NEGRAS posicional         |
|  <n>. <SAN>            |  Prof / Eval / Nodos ...    |
|  <comentario>          |  Mejor <jugada>             |
+------------------------+-----------------------------+
```

## Motor y memoria

- `DemoEngine = Searcher<ChessRules, StyledEval, ChessOrdering, 16384, false>`: el
  buscador genérico (negamax + alpha-beta + ID + quiescence + TT + PV) con tabla de
  transposición de 16384 entradas.
- El objeto de la partida es `static ChessMatch game;` (~400 kB): dos TT de 16384
  entradas viven en estática, no en la pila del 68000.
- `MinimalBackend::configure_memory({96 kB, 16 kB, 4 kB})` (Chip, Slow, frame); los
  bitplanes EHB se reservan de Chip RAM.
- Render por bytes alineados (`fill_bytes`, `draw_char_bytes`): el tablero y los
  paneles se escriben byte a byte por plano, y el refresco en vivo de los paneles es
  **no destructivo** (líneas rellenadas a ancho fijo), de modo que una captura a
  mitad de frame nunca muestra un panel en negro.

## Build / run / analyze

```bash
bash tools/build/build-demo.sh demos/amiga/123_chess_match --debug --clean
bash tools/run/run-demo.sh demos/amiga/123_chess_match --warp
bash tools/analyze/analyze-demo.sh demos/amiga/123_chess_match
```

Para observar la partida avanzar en la captura, ampliar el asentamiento:

```bash
bash tools/run/run-demo.sh demos/amiga/123_chess_match --warp --settle-ms 12000
```

## Estado

- **Compila y enlaza** (`build-demo.sh`, verificado en debug y release).
- **Ejecutado en WinUAE** (`run-demo.sh --warp`): READY + captura; con
  `--settle-ms 12000` se ha verificado `1. e4`, el comentario del juez, el resalte
  amarillo y el turno de las negras; `analyze-demo.sh` da **OK**.
- **Pendiente**: sondeo de tablas de finales en la partida, límite real de tiempo por
  reloj (hoy solo hay incremento), y modo "humano vs. máquina" con joystick.
