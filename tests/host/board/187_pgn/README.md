# HOST-187: PGN y libro de aperturas incorporado

Test host de `eng/board/rules/chess/pgn.hpp` y
`eng/board/rules/chess/opening_book.hpp`, las dos piezas que comparten la demo
`demos/features/board/chess/amiga/123_chess_match` y la simulación host `tools/board/selfplay.cpp`.

## Qué enseña / comprueba

- **PGN**: `PgnWriter` vuelca cabeceras, jugadas SAN, comentarios y resultado en un
  `Span<char>` sin heap ni I/O. El test compara el texto exacto de una partida corta
  (cabeceras, `1. e4 {book} e5`, resultado) y comprueba que un buffer pequeño se
  marca como truncado sin perder la longitud lógica.
- **Libro de aperturas incorporado**: `build_opening_book` construye las entradas a
  partir de las líneas UCI y `find_move_uci` resuelve una jugada legal por su UCI
  (y rechaza una ilegal). Tras `1. e4`, `probe_opening_book` encuentra la respuesta
  de libro y `opening_name` devuelve el nombre de la apertura.

## Salida de referencia

```
PGN y libro de aperturas:
OK: PGN (texto exacto, truncado) y libro de aperturas
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/board/187_pgn
```
