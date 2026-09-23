# HOST-151: E/S real de bloques desde un fichero del PC

Test host de `eng/board/storage/file_block_source.hpp` (y del flujo completo del
conocimiento en PC).

## Qué enseña / comprueba

`FileBlockSource` implementa el concept `BlockSource` leyendo bloques de un fichero
con `std::FILE`: es la E/S real del lado host, simétrica al trackloader del Amiga
(todavía pendiente). El test recorre la cadena completa:

```
entradas -> bytes (ByteWriter) -> fichero -> FileBlockSource -> BlockCache
         -> ByteReader -> BookEntry -> probe_book
```

- `open` fija el tamaño de bloque y calcula el número de bloques.
- `BlockCache<FileBlockSource, 12, 2>` sirve cada entrada como un bloque; el
  contenido recuperado coincide con el empaquetado.
- Una ruta inexistente falla limpiamente; el fichero temporal se borra al final.

## Salida de referencia

```
eng::board file source:
OK: file source (empaquetar -> fichero -> bloques -> probe)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/151_board_file
```
