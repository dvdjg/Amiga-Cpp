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

Compilación: `pack-book.sh` usa `CXX` (por defecto `g++` del PATH) con
`-I engine/include`.
