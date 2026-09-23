# HOST-147: libro de aperturas y tablas de finales

Test host de `eng/board/knowledge/{book,endgame_tables,cache}.hpp`.

## Qué enseña / comprueba

- **Libro de aperturas**: entradas ordenadas por clave Zobrist (12 B:
  `key|move|score|name_id`), búsqueda binaria (`probe_book`) y nombres en un pool
  NUL-separado (`book_name`).
- **Tablas de finales**: entradas de 8 B (`key|value|dtm|reservado`) con consulta por
  clave (`probe_endgame_table`).
- **Round-trip sin alineación**: los structs se serializan byte a byte
  (`write_book_entry`/`read_book_entry`) y viajan por un bloque; en 68000 un
  `reinterpret_cast` a struct desalineado daría bus error, por eso el test comprueba
  struct → bytes → bloque → struct.

## Salida de referencia

```
eng::board conocimiento:
OK: conocimiento (libro, nombres, round-trip por bloque, tablas de finales)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/board/147_board_knowledge
```
