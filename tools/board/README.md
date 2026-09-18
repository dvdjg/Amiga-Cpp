# `tools/board/` — empaquetado de conocimiento del motor de tablero

Herramientas host que convierten el conocimiento fuente (texto) en los blobs
binarios que sirve `eng::board` por bloques (`BlockSource`). Usan el propio engine
header-only, así que el binario es consistente con `probe_book`/`probe_endgame_table`.

## `pack-book.sh` — libro de aperturas

```bash
tools/board/pack-book.sh <entrada.txt> [salida.bin]
# salida por defecto: out/assets/board/book.bin
```

Formato de entrada (una línea por jugada; `#` para comentarios):

```
FEN ; uci ; score ; nombre-opcional
rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 ; e2e4 ; 20 ; Apertura
```

La clave Zobrist y la jugada se resuelven con las reglas reales del engine. El
blob son entradas `BookEntry` de 12 B ordenadas por clave; se lee con
`FileBlockSource` (PC) o, en el Amiga, con el trackloader (pendiente).

## Pendiente

- Empaquetado de **tablas de finales** y de **patrones de Go**.
- Volcado de nombres de apertura (pool NUL-separado).
- El backend de **disquete Amiga** (trackloader de hardware).

## `arena.sh` — torneos rápidos (con variantes)

```bash
tools/board/arena.sh [games] [depth] [variant] [seed] [max_plies] [nodes]
# por defecto: 10 2 chess960 0 60 20000
```

Enfrenta al motor consigo mismo en N partidas rápidas con presupuesto de nodos por
jugada. `variant` admite `standard` o `chess960` (arranques con **piezas
descolocadas**); la semilla elige la disposición y es reproducible. Imprime el
marcador (blancas/negras/tablas/sin acabar).

Compilación: `pack-book.sh` usa `CXX` (por defecto `g++` del PATH) con
`-I engine/include`.

## `selfplay.sh` — partidas completas de la demo (con PGN)

```bash
tools/board/selfplay.sh [games] [--variant standard|chess960] [--seed N]
  [--max-plies N] [--out ruta.pgn] [--swap] [--no-book]
  [--slice-nodes N] [--frames N] [--min-frames N] [--quiet]
# por defecto: 1 standard 0 300 out/board/selfplay/selfplay.pgn
```

Juega partidas completas **sin UI** con la misma configuración que la demo
`demos/amiga/123_chess_match`: estilos agresivo (blancas) y posicional (negras)
sobre el mismo `StyledEval`, libro de aperturas incorporado, búsqueda por rebanadas
(200 nodos por frame, hasta 20 frames) y relojes de 5:00 sin incremento. Los valores
por defecto son los de la demo; `--slice-nodes`/`--frames` permiten simular más
fuerte para análisis.

Cada partida se exporta a **PGN** con cabeceras, jugadas SAN, comentarios por jugada
(`libro` o `d<prof> <eval> n<nodos>`) y el resultado final. `--swap` alterna los
estilos entre blancas y negras. El resumen por consola da el marcador y la media de
plies/nodos.

Ejemplo:

```bash
tools/board/selfplay.sh 4 --swap --max-plies 400 --out out/board/selfplay/demo.pgn
```
