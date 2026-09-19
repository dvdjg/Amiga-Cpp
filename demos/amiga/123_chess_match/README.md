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
- **Relojes** de 5:00 sin incremento. El presupuesto de pensamiento por jugada se
  deriva del reloj restante (~1/25 de lo que queda, acotado entre 3 y 8 frames), de
  modo que el bando con menos tiempo piensa menos; si el reloj llega a cero, el juez
  declara `TIEMPO AGOTADO`.
- **Búsqueda por rebanadas** (32 nodos por frame, hasta 8 frames por jugada) para no
  bloquear el frame; el libro de aperturas lo usan los dos bandos, así que la
  apertura se juega al instante. El presupuesto es reducido porque la generación de
  jugadas es el cuello medido en el A500; ver `docs/debugging/BOARD_SELFPLAY_AND_PERF.md`.
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

## Rendimiento

El coste por frame lo domina la búsqueda (generación de jugadas + evaluación por
nodo). Medido con el contador de plies del `RunStatus` y comparando build debug y
release, ambos avanzan lo mismo, así que el cuello no es la generación de código sino
el coste por nodo y el ritmo del emulador (el sampler
`tools/profile/hotspots.mjs` no obtuvo muestras en este entorno). Decisión: **no
portar `movegen`/`eval` a asm todavía**; el libro en los dos bandos y el presupuesto
de pensamiento acotado mantienen la partida fluida. Si se quiere más profundidad por
jugada, el siguiente paso es perfilar y optimizar `movegen`/`eval` antes de bajar más
`kSliceNodes`.

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

## Simulación host (partidas completas con PGN)

La misma configuración (estilos, libro, rebanadas y relojes) se puede jugar sin UI en
host y exportar a PGN para analizar el resultado:

```bash
tools/board/selfplay.sh 4 --swap --max-plies 400 --out out/board/selfplay/demo.pgn
```

Ver `tools/board/README.md`.

## Estado

- **Compila y enlaza** (`build-demo.sh`, verificado en debug y release).
- **Ejecutado en WinUAE** (`run-demo.sh --warp`): READY + captura; con
  `--settle-ms 12000` se ha verificado `1. e4`, el comentario del juez, el resalte
  amarillo y el turno de las negras; `analyze-demo.sh` da **OK**.
- **Partidas completas en host** (`tools/board/selfplay.sh`): con `--swap`, dos
  partidas terminadas y exportadas a PGN con `[Result]` correcto (una victoria de
  cada color); HOST-187 cubre el escritor PGN y el libro.
- **Pendiente**: sondeo de tablas de finales en la partida y modo "humano vs.
  máquina" con joystick.
