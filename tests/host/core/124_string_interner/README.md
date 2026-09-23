# HOST-124: internado de cadenas

Test host de `engine/include/eng/core/util/string_interner.hpp`:
`eng::util::StringInterner<MaxStrings, A>`, que deduplica texto por **contenido** y le da
un id `u16` estable, copiando los bytes una sola vez en un `Allocator` (arena).

## Qué comprueba

1. Deduplicación por contenido: el mismo texto (aunque venga de otro buffer) da el mismo
   id; un prefijo distinto da otro id.
2. `lookup` id → texto; id inexistente → vista vacía.
3. Cadena vacía → `invalid`; capacidad de cadenas agotada.
4. Sin bytes en la arena → `invalid`.
5. `clear` (el asignador conserva sus bytes; su vida la lleva el llamador).

## Salida de referencia

```
StringInterner:
OK: StringInterner (dedup por contenido, lookup, capacidad, arena)
```

## Ejecutar

```bash
CXX="/c/.../mingw64/bin/g++.exe" bash tools/run-host-tests.sh tests/host/core/124_string_interner
```
